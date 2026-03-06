using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using Zlink.Service;
using static PerfRunner;

internal static class PerfGatewayClient
{
    private const string Pattern = "GATEWAY";
    private const string ServerServiceName = "perf-gateway";
    private const string ClientServicePrefix = "c";
    private const uint RunId = 1;

    internal static int Run(string transport, int size, string endpoint)
    {
        GatewayClientConfig config = BuildConfig(transport, size);

        if (!TryParseGatewayReadyEndpoint(endpoint, out _, out string registryPub,
                out string registryRouter))
        {
            Console.Error.WriteLine("multi_client_error:invalid_gateway_ready_payload");
            return 1;
        }

        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx);
        Discovery? discovery = null;
        var slots = new List<GatewayClientSlot>(config.ClientCount);
        try
        {
            discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
            ApplyDiscoverySocketOptions(discovery, Pattern, config.SndTimeoutMs,
                config.RcvTimeoutMs);
            discovery.ConnectRegistry(registryPub);
            discovery.Subscribe(ServerServiceName);
            if (!WaitUntil(() => discovery.ReceiverCount(ServerServiceName) > 0,
                    config.ReadyTimeoutMs))
            {
                Console.Error.WriteLine(
                    "multi_client_error:gateway_discovery_not_ready");
                return 2;
            }

            for (int i = 0; i < config.ClientCount; i++)
            {
                string clientServiceName = $"{ClientServicePrefix}{i}";
                GatewayClientSlot slot = CreateGatewayClientSlot(ctx, discovery,
                    config.Transport, Pattern, registryRouter,
                    clientServiceName, i, config.SndTimeoutMs,
                    config.RcvTimeoutMs);
                slots.Add(slot);
            }

            List<GatewayClientSlot> activeSlots = CollectReadyGatewaySlots(slots,
                ServerServiceName, config.ReadyTimeoutMs);
            activeSlots = CollectEchoReadyGatewaySlots(activeSlots,
                ServerServiceName, config.Size, config.ReadyTimeoutMs);
            if (activeSlots.Count == 0)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }

            var payload = new byte[Math.Max(config.Size, PerfMetricHeaderSize)];
            Array.Fill(payload, (byte)'a');
            var routingId = new byte[256];
            var recv = new byte[Math.Max(256, Math.Max(config.Size,
                MultiStopToken.Length))];
            GatewayClientResult result = RunMultiGatewayClientLoop(activeSlots,
                payload, routingId, recv, config);

            TrySendGatewayStopToken(activeSlots, ServerServiceName);

            PrintResult(Pattern, config.Transport, config.Size, result.Throughput,
                result.LatencyUs, result.LatencyP95Us, result.LatencyP99Us);
            return 0;
        }
        finally
        {
            DisposeGatewaySlots(slots);
            TryDisposeQuietly(discovery);
        }
    }

    private static GatewayClientConfig BuildConfig(string transport, int size)
    {
        int resolvedSize = Math.Max(1, size);
        int drainMs = ResolveMultiDrainMs(Pattern);
        return new GatewayClientConfig(
            transport,
            resolvedSize,
            ResolveMultiWarmupSeconds(),
            ResolveMultiDurationSeconds(),
            ResolveMultiSettleMs(),
            drainMs,
            ResolveMultiSizeTransitionDrainMs(),
            ResolveMultiActiveWarmup(),
            ResolveMultiWarmupDrainMs(drainMs),
            ResolveMultiSndTimeoutMs(),
            ResolveMultiRcvTimeoutMs(),
            ResolveMultiConnectReadyTimeoutMs(),
            ResolveMultiLatencySampleCap(),
            ResolveMultiClients(Pattern));
    }

    private static GatewayClientResult
        RunMultiGatewayClientLoop(List<GatewayClientSlot> activeSlots,
            byte[] payload, byte[] routingId, byte[] recv,
            GatewayClientConfig config)
    {
        var phaseState = new GatewayClientPhaseState(config.LatencySampleCap);
        RunWarmupPhase(activeSlots, payload, routingId, recv, config,
            ref phaseState);
        RunSettlePhase(config);
        GatewayClientActiveStats activeStats = RunActivePhase(activeSlots, payload,
            routingId, recv, config, ref phaseState);
        RunDrainPhase(config);
        return ComputeResult(activeStats, phaseState.LatencySamples);
    }

    private static void RunWarmupPhase(List<GatewayClientSlot> activeSlots,
        byte[] payload, byte[] routingId, byte[] recv, GatewayClientConfig config,
        ref GatewayClientPhaseState state)
    {
        if (config.ActiveWarmup)
        {
            long warmupDeadlineTicks = Stopwatch.GetTimestamp()
                + (long)Math.Max(0, config.WarmupSeconds) * Stopwatch.Frequency;
            while (Stopwatch.GetTimestamp() < warmupDeadlineTicks)
            {
                GatewayClientSlot slot = activeSlots[state.Index];
                StampMetricHeader(payload.AsSpan(), RunId, PerfPhase.Warmup,
                    config.Size, state.Seq++, EpochUs());
                if (TrySendGatewayPayload(slot.Gateway, ServerServiceName,
                        payload.AsSpan()))
                {
                    _ = TryReceiveGatewayEcho(slot.ReceiverRouter, routingId,
                        recv, out _);
                }

                AdvanceRoundRobin(ref state.Index, activeSlots.Count);
            }

            if (config.WarmupDrainMs > 0)
                Thread.Sleep(config.WarmupDrainMs);
            return;
        }

        if (config.WarmupSeconds > 0)
            Thread.Sleep(config.WarmupSeconds * 1000);
    }

    private static void RunSettlePhase(GatewayClientConfig config)
    {
        if (config.SettleMs > 0)
            Thread.Sleep(config.SettleMs);
    }

    private static GatewayClientActiveStats RunActivePhase(
        List<GatewayClientSlot> activeSlots, byte[] payload, byte[] routingId,
        byte[] recv, GatewayClientConfig config, ref GatewayClientPhaseState state)
    {
        long measureCount = 0;
        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, config.DurationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            GatewayClientSlot slot = activeSlots[state.Index];
            long beginTicks = Stopwatch.GetTimestamp();
            StampMetricHeader(payload.AsSpan(), RunId, PerfPhase.Active, config.Size,
                state.Seq++, EpochUs());
            if (!TrySendGatewayPayload(slot.Gateway, ServerServiceName,
                    payload.AsSpan()))
            {
                AdvanceRoundRobin(ref state.Index, activeSlots.Count);
                continue;
            }

            if (!TryReceiveGatewayEcho(slot.ReceiverRouter, routingId, recv,
                    out int received) || received <= 0)
            {
                AdvanceRoundRobin(ref state.Index, activeSlots.Count);
                continue;
            }

            ReadOnlySpan<byte> body = recv.AsSpan(0, received);
            if (!TryDecodeMetricHeader(body, out PerfMetricHeader header)
                || header.RunId != RunId
                || header.MsgSize != (uint)config.Size
                || header.Phase != (uint)PerfPhase.Active)
            {
                AdvanceRoundRobin(ref state.Index, activeSlots.Count);
                continue;
            }

            long endTicks = Stopwatch.GetTimestamp();
            measureCount++;
            double oneWayLatencyUs = ((endTicks - beginTicks) * 1_000_000.0
                                      / Stopwatch.Frequency) / 2.0;
            ReservoirSample(state.LatencySamples, oneWayLatencyUs,
                ref state.SampleSeen, config.LatencySampleCap, ref state.Rng);
            AdvanceRoundRobin(ref state.Index, activeSlots.Count);
        }

        long benchEndTicks = Stopwatch.GetTimestamp();
        return new GatewayClientActiveStats(measureCount, benchStartTicks,
            benchEndTicks);
    }

    private static void RunDrainPhase(GatewayClientConfig config)
    {
        if (config.DrainMs > 0)
            Thread.Sleep(config.DrainMs);
        if (config.SizeTransitionDrainMs > 0)
            Thread.Sleep(config.SizeTransitionDrainMs);
    }

    private static GatewayClientResult ComputeResult(
        GatewayClientActiveStats activeStats, List<double> latencySamples)
    {
        double elapsedSeconds = (activeStats.BenchEndTicks
                                 - activeStats.BenchStartTicks)
            / (double)Stopwatch.Frequency;
        double throughput = elapsedSeconds > 0.0
            ? activeStats.MeasureCount / elapsedSeconds
            : 0.0;
        double fallbackLatencyUs = (elapsedSeconds * 1_000_000.0)
            / Math.Max(1.0, activeStats.MeasureCount * 2.0);
        var latency = ComputeLatencyStats(latencySamples);
        double latencyUs = latency.mean > 0.0 ? latency.mean : fallbackLatencyUs;
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;
        return new GatewayClientResult(throughput, latencyUs, latencyP95Us,
            latencyP99Us);
    }

    private static void AdvanceRoundRobin(ref int index, int count)
    {
        index++;
        if (index == count)
            index = 0;
    }

    private static GatewayClientSlot CreateGatewayClientSlot(Context ctx,
        Discovery discovery, string transport, string pattern,
        string registryRouter, string clientServiceName,
        int index, int sndTimeoutMs, int rcvTimeoutMs)
    {
        var receiver = new Receiver(ctx, clientServiceName);
        ConfigureReceiverTlsServerIfNeeded(receiver, transport);
        string receiverEndpoint = EndpointFor(transport,
            $"multi-gateway-client-{index}");
        receiver.Bind(receiverEndpoint);
        receiver.ConnectRegistry(registryRouter);
        receiver.Register(clientServiceName, receiverEndpoint, 1);
        ApplyReceiverSocketOptions(receiver, pattern, sndTimeoutMs, rcvTimeoutMs);
        Zlink.Socket receiverRouter = receiver.CreateRouterSocket();
        ApplyMultiSocketOptions(receiverRouter, pattern);
        receiverRouter.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);

        var gateway = new Gateway(ctx, discovery, clientServiceName);
        ConfigureGatewayTlsClientIfNeeded(gateway, transport);
        gateway.SetOption(SocketOptions.SndHwm,
            ResolveMultiHwmValue("PERF_SNDHWM", pattern));
        gateway.SetOption(SocketOptions.RcvHwm,
            ResolveMultiHwmValue("PERF_RCVHWM", pattern));
        gateway.SetOption(SocketOptions.Linger, 0);
        gateway.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
        gateway.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);

        return new GatewayClientSlot(receiver, receiverRouter, gateway);
    }

    private static bool TryReceiveGatewayEcho(Zlink.Socket receiverRouter,
        byte[] routingIdBuffer, byte[] payloadBuffer, out int payloadLength)
    {
        payloadLength = 0;
        int ridLen = ReceiveRetry(receiverRouter, routingIdBuffer.AsSpan(),
            ReceiveFlags.DontWait);
        if (ridLen <= 0)
            return false;

        long deadlineTicks = Stopwatch.GetTimestamp()
            + (Stopwatch.Frequency / 1000);
        while (true)
        {
            int n = ReceiveRetry(receiverRouter, payloadBuffer.AsSpan(),
                ReceiveFlags.DontWait);
            if (n > 0)
            {
                payloadLength = n;
                return true;
            }

            if (Stopwatch.GetTimestamp() >= deadlineTicks)
                return false;

            Thread.Yield();
        }
    }

    private static bool TrySendGatewayPayload(Gateway gateway,
        string serviceName, ReadOnlySpan<byte> payload)
    {
        gateway.Send(serviceName, payload, SendFlags.None);
        return true;
    }

    private static bool TryParseGatewayReadyEndpoint(string endpoint,
        out string serverEndpoint, out string registryPub,
        out string registryRouter)
    {
        serverEndpoint = string.Empty;
        registryPub = string.Empty;
        registryRouter = string.Empty;

        if (string.IsNullOrWhiteSpace(endpoint))
            return false;
        string[] parts = endpoint.Split('|', 3, StringSplitOptions.TrimEntries);
        if (parts.Length != 3)
            return false;

        serverEndpoint = parts[0];
        registryPub = parts[1];
        registryRouter = parts[2];
        return !string.IsNullOrWhiteSpace(serverEndpoint)
               && !string.IsNullOrWhiteSpace(registryPub)
               && !string.IsNullOrWhiteSpace(registryRouter);
    }

    private static List<GatewayClientSlot> CollectReadyGatewaySlots(
        IReadOnlyList<GatewayClientSlot> slots, string serverServiceName,
        int readyTimeoutMs)
    {
        var activeSlots = new List<GatewayClientSlot>(slots.Count);
        if (slots.Count == 0)
            return activeSlots;

        var ready = new bool[slots.Count];
        int remaining = slots.Count;
        long deadlineTicks = DeadlineTicksFromMilliseconds(readyTimeoutMs);

        while (remaining > 0 && Stopwatch.GetTimestamp() < deadlineTicks)
        {
            bool progressed = false;
            for (int i = 0; i < slots.Count; i++)
            {
                if (ready[i])
                    continue;

                bool connected;
                try
                {
                    connected = slots[i].Gateway.ConnectionCount(serverServiceName)
                                > 0;
                }
                catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                                || IsInterrupted(ex.Errno))
                {
                    connected = false;
                }

                if (!connected)
                    continue;

                ready[i] = true;
                activeSlots.Add(slots[i]);
                remaining--;
                progressed = true;
            }

            if (!progressed)
                Thread.Sleep(1);
        }

        return activeSlots;
    }

    private static List<GatewayClientSlot> CollectEchoReadyGatewaySlots(
        IReadOnlyList<GatewayClientSlot> slots, string serverServiceName, int msgSize,
        int readyTimeoutMs)
    {
        var echoReady = new List<GatewayClientSlot>(slots.Count);
        if (slots.Count == 0)
            return echoReady;

        var payload = new byte[Math.Max(msgSize, PerfMetricHeaderSize)];
        Array.Fill(payload, (byte)'p');
        var routingId = new byte[256];
        var recv = new byte[Math.Max(256, Math.Max(msgSize, MultiStopToken.Length))];
        ulong seq = 1;

        for (int i = 0; i < slots.Count; i++)
        {
            GatewayClientSlot slot = slots[i];
            bool ready = false;
            long slotDeadlineTicks = DeadlineTicksFromMilliseconds(readyTimeoutMs);
            while (Stopwatch.GetTimestamp() < slotDeadlineTicks)
            {
                if (!IsGatewayConnected(slot.Gateway, serverServiceName))
                {
                    Thread.Sleep(1);
                    continue;
                }

                StampMetricHeader(payload.AsSpan(), 1, PerfPhase.Warmup,
                    msgSize, seq++, EpochUs());
                if (!TrySendGatewayPayload(slot.Gateway, serverServiceName,
                        payload.AsSpan()))
                {
                    Thread.Sleep(1);
                    continue;
                }

                if (TryReceiveGatewayEcho(slot.ReceiverRouter, routingId, recv,
                        out int n) && n > 0)
                {
                    ready = true;
                    break;
                }
            }

            if (ready)
                echoReady.Add(slot);
        }

        return echoReady;
    }

    private static bool IsGatewayConnected(Gateway gateway, string serviceName)
    {
        try
        {
            return gateway.ConnectionCount(serviceName) > 0;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno))
        {
            return false;
        }
    }

    private static void TrySendGatewayStopToken(
        IReadOnlyList<GatewayClientSlot> activeSlots, string serviceName)
    {
        if (activeSlots == null || activeSlots.Count == 0)
            return;

        for (int i = 0; i < activeSlots.Count; i++)
        {
            _ = TrySendGatewayPayload(activeSlots[i].Gateway, serviceName,
                MultiStopToken.AsSpan());
        }
    }

    private static void ApplyReceiverSocketOptions(Receiver receiver,
        string pattern, int sndTimeoutMs, int rcvTimeoutMs)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", pattern);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", pattern);
        receiver.SetOption(ReceiverSocketRole.Router, SocketOptions.Linger, 0);
        receiver.SetOption(ReceiverSocketRole.Dealer, SocketOptions.Linger, 0);
        receiver.SetOption(ReceiverSocketRole.Router, SocketOptions.SndHwm, sndHwm);
        receiver.SetOption(ReceiverSocketRole.Router, SocketOptions.RcvHwm, rcvHwm);
        receiver.SetOption(ReceiverSocketRole.Dealer, SocketOptions.SndHwm, sndHwm);
        receiver.SetOption(ReceiverSocketRole.Dealer, SocketOptions.RcvHwm, rcvHwm);
        receiver.SetOption(ReceiverSocketRole.Router, SocketOptions.SndTimeo,
            sndTimeoutMs);
        receiver.SetOption(ReceiverSocketRole.Router, SocketOptions.RcvTimeo,
            rcvTimeoutMs);
        receiver.SetOption(ReceiverSocketRole.Dealer, SocketOptions.SndTimeo,
            sndTimeoutMs);
        receiver.SetOption(ReceiverSocketRole.Dealer, SocketOptions.RcvTimeo,
            rcvTimeoutMs);
    }

    private static void ApplyDiscoverySocketOptions(Discovery discovery,
        string pattern, int sndTimeoutMs, int rcvTimeoutMs)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", pattern);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", pattern);
        discovery.SetOption(DiscoverySocketRole.Sub, SocketOptions.Linger, 0);
        discovery.SetOption(DiscoverySocketRole.Sub, SocketOptions.SndHwm, sndHwm);
        discovery.SetOption(DiscoverySocketRole.Sub, SocketOptions.RcvHwm, rcvHwm);
        discovery.SetOption(DiscoverySocketRole.Sub, SocketOptions.SndTimeo,
            sndTimeoutMs);
        discovery.SetOption(DiscoverySocketRole.Sub, SocketOptions.RcvTimeo,
            rcvTimeoutMs);
    }

    private static void DisposeGatewaySlots(IReadOnlyList<GatewayClientSlot> slots)
    {
        for (int i = 0; i < slots.Count; i++)
            TryDisposeQuietly(slots[i]);
    }

    private readonly struct GatewayClientConfig
    {
        internal GatewayClientConfig(string transport, int size, int warmupSeconds,
            int durationSeconds, int settleMs, int drainMs,
            int sizeTransitionDrainMs, bool activeWarmup, int warmupDrainMs,
            int sndTimeoutMs, int rcvTimeoutMs, int readyTimeoutMs,
            int latencySampleCap, int clientCount)
        {
            Transport = transport;
            Size = size;
            WarmupSeconds = warmupSeconds;
            DurationSeconds = durationSeconds;
            SettleMs = settleMs;
            DrainMs = drainMs;
            SizeTransitionDrainMs = sizeTransitionDrainMs;
            ActiveWarmup = activeWarmup;
            WarmupDrainMs = warmupDrainMs;
            SndTimeoutMs = sndTimeoutMs;
            RcvTimeoutMs = rcvTimeoutMs;
            ReadyTimeoutMs = readyTimeoutMs;
            LatencySampleCap = latencySampleCap;
            ClientCount = clientCount;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int WarmupSeconds { get; }
        internal int DurationSeconds { get; }
        internal int SettleMs { get; }
        internal int DrainMs { get; }
        internal int SizeTransitionDrainMs { get; }
        internal bool ActiveWarmup { get; }
        internal int WarmupDrainMs { get; }
        internal int SndTimeoutMs { get; }
        internal int RcvTimeoutMs { get; }
        internal int ReadyTimeoutMs { get; }
        internal int LatencySampleCap { get; }
        internal int ClientCount { get; }
    }

    private readonly struct GatewayClientResult
    {
        internal GatewayClientResult(double throughput, double latencyUs,
            double latencyP95Us, double latencyP99Us)
        {
            Throughput = throughput;
            LatencyUs = latencyUs;
            LatencyP95Us = latencyP95Us;
            LatencyP99Us = latencyP99Us;
        }

        internal double Throughput { get; }
        internal double LatencyUs { get; }
        internal double LatencyP95Us { get; }
        internal double LatencyP99Us { get; }
    }

    private readonly struct GatewayClientActiveStats
    {
        internal GatewayClientActiveStats(long measureCount,
            long benchStartTicks, long benchEndTicks)
        {
            MeasureCount = measureCount;
            BenchStartTicks = benchStartTicks;
            BenchEndTicks = benchEndTicks;
        }

        internal long MeasureCount { get; }
        internal long BenchStartTicks { get; }
        internal long BenchEndTicks { get; }
    }

    private struct GatewayClientPhaseState
    {
        internal GatewayClientPhaseState(int latencySampleCap)
        {
            Index = 0;
            Seq = 1;
            SampleSeen = 0;
            Rng = 0xA341316Cu;
            LatencySamples = new List<double>(latencySampleCap);
        }

        internal int Index;
        internal ulong Seq;
        internal long SampleSeen;
        internal uint Rng;
        internal List<double> LatencySamples;
    }

    private sealed class GatewayClientSlot : IDisposable
    {
        internal GatewayClientSlot(Receiver receiver, Zlink.Socket receiverRouter,
            Gateway gateway)
        {
            Receiver = receiver;
            ReceiverRouter = receiverRouter;
            Gateway = gateway;
        }

        internal Receiver Receiver { get; }
        internal Zlink.Socket ReceiverRouter { get; }
        internal Gateway Gateway { get; }

        public void Dispose()
        {
            TryDisposeQuietly(ReceiverRouter);
            TryDisposeQuietly(Gateway);
            TryDisposeQuietly(Receiver);
        }
    }
}
