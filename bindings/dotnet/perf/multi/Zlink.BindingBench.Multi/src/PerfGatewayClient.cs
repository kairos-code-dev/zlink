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
    private const string ServerServiceName = "perf-server";
    private const string ClientServicePrefix = "c";
    private const uint RunId = 1;

    internal static int Run(string transport, int size, string endpoint)
    {
        GatewayClientConfig config = BuildConfig(transport, size);

        if (!TryParseGatewayReadyEndpoint(endpoint, out _, out _,
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
            ConnectRegistryWithRetry(() => discovery.ConnectRegistry(
                registryRouter));
            discovery.Subscribe(ServerServiceName);

            for (int i = 0; i < config.ClientCount; i++)
            {
                string clientServiceName = $"{ClientServicePrefix}{i}";
                GatewayClientSlot slot = CreateGatewayClientSlot(ctx, discovery,
                    config.Transport, Pattern, registryRouter,
                    clientServiceName, null, i, config.Size,
                    config.ReadyTimeoutMs,
                    config.SndTimeoutMs, config.RcvTimeoutMs);
                slots.Add(slot);
            }

            if (slots.Count == 0)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }

            if (!WaitUntil(() =>
                    discovery.ServiceAvailable(ServerServiceName),
                    checked(config.ReadyTimeoutMs * 4), 10))
            {
                Console.Error.WriteLine(
                    "multi_client_error:gateway_discovery_not_ready");
                return 2;
            }

            string? serverRoutingId = ResolveServerRoutingId(discovery);
            for (int i = 0; i < slots.Count; i++)
                slots[i].ServerRoutingId = serverRoutingId;

            if (!PrimeRoundtripAllSlots(slots, config))
            {
                Console.Error.WriteLine("multi_client_error:gateway_prime_failed");
                return 2;
            }

            GatewayClientResult result = RunMultiGatewayClientLoop(slots,
                config);

            TrySendGatewayStopToken(slots);

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
            ResolveMultiSndTimeoutMs(),
            ResolveMultiRcvTimeoutMs(),
            ResolveMultiConnectReadyTimeoutMs(),
            ResolveMultiClientPollTimeoutMs(),
            ResolveMultiLatencySampleCap(),
            ResolveMultiClients(Pattern));
    }

    private static GatewayClientResult
        RunMultiGatewayClientLoop(List<GatewayClientSlot> activeSlots,
            GatewayClientConfig config)
    {
        var phaseState = new GatewayClientPhaseState(config.LatencySampleCap);
        phaseState.EnsureSlotState(activeSlots.Count);

        RunWarmupPhase(activeSlots, config, ref phaseState);
        if (!RunSettlePhase(activeSlots, config, ref phaseState))
            return GatewayClientResult.Empty;

        GatewayClientActiveStats activeStats = RunActivePhase(
            activeSlots, config, ref phaseState);
        if (!activeStats.HasValue)
            return GatewayClientResult.Empty;

        RunDrainPhase(config);
        return ComputeResult(activeStats, phaseState.LatencySamples,
            config.DurationSeconds);
    }

    private static void RunWarmupPhase(List<GatewayClientSlot> activeSlots,
        GatewayClientConfig config, ref GatewayClientPhaseState state)
    {
        if (config.WarmupSeconds <= 0)
            return;

        long deadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(0, config.WarmupSeconds) * Stopwatch.Frequency;
        _ = RunEchoPhaseLoop(activeSlots, config.Size, RunId, PerfPhase.Warmup,
            deadlineTicks, allowSend: true, collectMetrics: false, config,
            ref state);
    }

    private static bool RunSettlePhase(List<GatewayClientSlot> activeSlots,
        GatewayClientConfig config, ref GatewayClientPhaseState state)
    {
        if (config.SettleMs > 0)
        {
            long deadlineTicks = Stopwatch.GetTimestamp()
                + MillisecondsToTicks(config.SettleMs);
            _ = RunEchoPhaseLoop(activeSlots, config.Size, RunId,
                PerfPhase.Warmup, deadlineTicks, allowSend: false,
                collectMetrics: false, config, ref state);
        }

        return DrainPendingReplies(activeSlots, config, ref state);
    }

    private static GatewayClientActiveStats RunActivePhase(
        List<GatewayClientSlot> activeSlots, GatewayClientConfig config,
        ref GatewayClientPhaseState state)
    {
        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, config.DurationSeconds) * Stopwatch.Frequency;
        long measureCount = RunEchoPhaseLoop(activeSlots, config.Size, RunId,
            PerfPhase.Active, benchDeadlineTicks, allowSend: true,
            collectMetrics: true, config, ref state);
        long benchEndTicks = Stopwatch.GetTimestamp();
        return new GatewayClientActiveStats(measureCount, benchStartTicks,
            benchEndTicks, measureCount > 0);
    }

    private static void RunDrainPhase(GatewayClientConfig config)
    {
        if (config.DrainMs > 0)
            Thread.Sleep(config.DrainMs);
        if (config.SizeTransitionDrainMs > 0)
            Thread.Sleep(config.SizeTransitionDrainMs);
    }

    private static GatewayClientResult ComputeResult(
        GatewayClientActiveStats activeStats, List<double> latencySamples,
        int durationSeconds)
    {
        if (!activeStats.HasValue)
            return GatewayClientResult.Empty;

        double elapsedSeconds = Math.Max(1.0, durationSeconds);
        double throughput = activeStats.MeasureCount / elapsedSeconds;
        double measuredSeconds = (activeStats.BenchEndTicks
                                  - activeStats.BenchStartTicks)
            / (double)Stopwatch.Frequency;
        double fallbackLatencyUs = (elapsedSeconds * 1_000_000.0)
            / Math.Max(1.0, activeStats.MeasureCount * 2.0);
        var latency = ComputeLatencyStats(latencySamples);
        double latencyUs = latency.mean > 0.0 ? latency.mean : Math.Max(
            fallbackLatencyUs, (measuredSeconds * 1_000_000.0)
            / Math.Max(1.0, activeStats.MeasureCount * 2.0));
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;
        return new GatewayClientResult(throughput, latencyUs, latencyP95Us,
            latencyP99Us);
    }

    private static int EffectiveClientPollTimeoutMs(int timeoutMs)
    {
        return Math.Max(1, timeoutMs);
    }

    private static GatewayClientSlot CreateGatewayClientSlot(Context ctx,
        Discovery discovery, string transport, string pattern,
        string registryRouter, string clientServiceName, string? serverRoutingId,
        int index, int payloadCapacity, int readyTimeoutMs, int sndTimeoutMs,
        int rcvTimeoutMs)
    {
        var receiver = new Receiver(ctx, clientServiceName);
        ConfigureReceiverTlsServerIfNeeded(receiver, transport);
        string receiverEndpoint = EndpointFor(transport,
            $"multi-gateway-client-{index}");
        receiver.Bind(receiverEndpoint);
        ConnectRegistryWithRetry(() => receiver.ConnectRegistry(registryRouter));
        receiver.Register(clientServiceName, receiverEndpoint, 1);
        if (!WaitUntil(() =>
                receiver.GetRegisterResult(clientServiceName).Status == 0,
                readyTimeoutMs, 10))
        {
            throw new TimeoutException(
                $"gateway client receiver register timeout: {clientServiceName}");
        }
        ApplyReceiverSocketOptions(receiver, pattern, sndTimeoutMs, rcvTimeoutMs);

        var gateway = new Gateway(ctx, discovery, clientServiceName);
        ConfigureGatewayTlsClientIfNeeded(gateway, transport);
        gateway.SetOption(SocketOptions.SndHwm,
            ResolveMultiHwmValue("PERF_SNDHWM", pattern));
        gateway.SetOption(SocketOptions.RcvHwm,
            ResolveMultiHwmValue("PERF_RCVHWM", pattern));
        gateway.SetOption(SocketOptions.Linger, 0);
        gateway.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
        gateway.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);

        return new GatewayClientSlot(receiver, gateway,
            gateway.PrepareService(ServerServiceName), serverRoutingId,
            Math.Max(Math.Max(payloadCapacity, PerfMetricHeaderSize), 256));
    }

    private static bool TryReceiveGatewayEcho(GatewayClientSlot slot,
        out int payloadLength)
    {
        try
        {
            payloadLength = slot.Receiver.ReceiveSinglePayload(slot.Recv.AsSpan(),
                ReceiveFlags.DontWait);
            return payloadLength > 0;
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                        || IsInterrupted(ex.Errno))
        {
            payloadLength = 0;
            return false;
        }
    }

    private static bool TrySendGatewayPayload(GatewayClientSlot slot,
        ReadOnlySpan<byte> payload)
    {
        try
        {
            if (string.IsNullOrEmpty(slot.ServerRoutingId))
            {
                slot.Gateway.Send(slot.ServerService, payload,
                    SendFlags.DontWait);
            }
            else
            {
                slot.Gateway.SendToRoutingId(ServerServiceName,
                    slot.ServerRoutingId, payload, SendFlags.DontWait);
            }
            return true;
        }
        catch (ZlinkException ex) when (IsGatewaySendBlocked(ex.Errno))
        {
            return false;
        }
    }

    private static bool IsGatewaySendBlocked(int errno)
    {
        return IsWouldBlock(errno) || IsInterrupted(errno)
               || errno == (int)ErrorCode.Efsm || errno == 2
               || errno == (int)ErrorCode.ENotConn
               || errno == (int)ErrorCode.EHostUnreach;
    }

    private static string? ResolveServerRoutingId(Discovery discovery)
    {
        ReceiverInfoRecord[] receivers = discovery.GetReceivers(ServerServiceName);
        if (receivers.Length == 0)
            return null;
        return string.IsNullOrWhiteSpace(receivers[0].RoutingId)
            ? null
            : receivers[0].RoutingId;
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

    private static void TrySendGatewayStopToken(
        IReadOnlyList<GatewayClientSlot> activeSlots)
    {
        if (activeSlots == null || activeSlots.Count == 0)
            return;

        for (int i = 0; i < activeSlots.Count; i++)
        {
            _ = TrySendGatewayPayload(activeSlots[i], MultiStopToken.AsSpan());
        }
    }

    private static long RunEchoPhaseLoop(
        IReadOnlyList<GatewayClientSlot> activeSlots, int msgSize, uint runId,
        PerfPhase phase, long deadlineTicks, bool allowSend,
        bool collectMetrics, GatewayClientConfig config,
        ref GatewayClientPhaseState state)
    {
        if (activeSlots.Count == 0)
            return 0;

        var sendPending = new byte[activeSlots.Count];
        long recvCount = 0;
        using var receiverPoller = new Poller();
        using var sendPoller = new Poller();
        var receiverEvents = new PollEvent[Math.Max(1, activeSlots.Count)];
        var sendEvents = new PollEvent[Math.Max(1, activeSlots.Count)];
        for (int i = 0; i < activeSlots.Count; i++)
        {
            receiverPoller.AddReceiver(activeSlots[i].Receiver,
                PollEvents.PollIn, i);
            sendPoller.AddGateway(activeSlots[i].Gateway, PollEvents.None, i);
        }

        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            if (allowSend)
            {
                int start = state.RoundRobinIndex;
                for (int attempts = 0; attempts < activeSlots.Count; attempts++)
                {
                    int index = (start + attempts) % activeSlots.Count;
                    if (!CanStartGatewaySend(state, sendPending, index,
                            activeSlots.Count))
                    {
                        continue;
                    }

                    GatewayPhaseSendResult sendResult = TryGatewayPhaseSend(
                        activeSlots, sendPoller, sendPending, msgSize, runId,
                        phase, ref state, index);
                    if (sendResult == GatewayPhaseSendResult.Fatal)
                        return recvCount;
                }

                state.RoundRobinIndex = activeSlots.Count == 0 ? 0
                    : (start + 1) % activeSlots.Count;
            }

            int written = receiverPoller.Wait(receiverEvents.AsSpan(),
                EffectiveClientPollTimeoutMs(config.ClientPollTimeoutMs),
                out int readyCount);
            if (readyCount > 0)
            {
                for (int eventIndex = 0; eventIndex < written; eventIndex++)
                {
                    if (receiverEvents[eventIndex].Tag is not int index)
                        continue;
                    GatewayClientSlot slot = activeSlots[index];
                    int n = 0;
                    try
                    {
                        n = slot.Receiver.ReceiveSinglePayload(slot.Recv.AsSpan(),
                            ReceiveFlags.DontWait);
                    }
                    catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                                    || IsInterrupted(ex.Errno))
                    {
                        n = 0;
                    }

                    if (n <= 0)
                        continue;

                    state.AwaitingReply[index] = 0;
                    if (TryDecodeMetricHeader(slot.Recv.AsSpan(0, n),
                            out PerfMetricHeader header)
                        && header.RunId == runId
                        && header.Phase == (uint)phase
                        && header.MsgSize == (uint)msgSize)
                    {
                        recvCount++;
                        if (collectMetrics)
                        {
                            ulong nowUs = EpochUs();
                            if (header.SentTsUs > 0 && nowUs >= header.SentTsUs)
                            {
                                double sampleUs = (nowUs - header.SentTsUs) * 0.5;
                                ReservoirSample(state.LatencySamples, sampleUs,
                                    ref state.SampleSeen, state.LatencySampleCap,
                                    ref state.Rng);
                            }
                        }
                    }

                    if (allowSend)
                    {
                    GatewayPhaseSendResult sendResult = TryGatewayPhaseSend(
                        activeSlots, sendPoller, sendPending, msgSize,
                        runId, phase, ref state, index);
                    if (sendResult == GatewayPhaseSendResult.Fatal)
                        return recvCount;
                }
                }
            }

            written = sendPoller.Wait(sendEvents.AsSpan(), 0, out readyCount);
            if (readyCount > 0)
            {
                for (int eventIndex = 0; eventIndex < written; eventIndex++)
                {
                    if (sendEvents[eventIndex].Tag is not int index)
                        continue;
                    if (!CanResumeGatewaySend(state, sendPending, index,
                            activeSlots.Count))
                    {
                        continue;
                    }

                    GatewayPhaseSendResult sendResult = TryGatewayPhaseSend(
                        activeSlots, sendPoller, sendPending, msgSize, runId,
                        phase, ref state, index);
                    if (sendResult == GatewayPhaseSendResult.Fatal)
                        return recvCount;
                }
            }

        }

        return recvCount;
    }

    private static GatewayPhaseSendResult TryGatewayPhaseSend(
        IReadOnlyList<GatewayClientSlot> activeSlots, Poller sendPoller,
        byte[] sendPending, int msgSize, uint runId, PerfPhase phase,
        ref GatewayClientPhaseState state, int index)
    {
        GatewayClientSlot slot = activeSlots[index];
        if (sendPending[index] == 0)
        {
            StampMetricHeader(slot.Payload.AsSpan(), runId, phase, msgSize,
                state.Seq++, EpochUs());
        }

        if (TrySendGatewayPayload(slot, slot.Payload.AsSpan()))
        {
            sendPending[index] = 0;
            sendPoller.ModifyGateway(slot.Gateway, PollEvents.None);
            state.AwaitingReply[index] = 1;
            return GatewayPhaseSendResult.Progressed;
        }

        sendPending[index] = 1;
        sendPoller.ModifyGateway(slot.Gateway, PollEvents.PollOut);
        return GatewayPhaseSendResult.Pending;
    }

    private static void ApplyReceiverSocketOptions(Receiver receiver,
        string pattern, int sndTimeoutMs, int rcvTimeoutMs)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", pattern);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", pattern);
        receiver.SetOption(SocketOptions.Linger, 0);
        receiver.SetOption(SocketOptions.SndHwm, sndHwm);
        receiver.SetOption(SocketOptions.RcvHwm, rcvHwm);
        receiver.SetOption(SocketOptions.SndTimeo, sndTimeoutMs);
        receiver.SetOption(SocketOptions.RcvTimeo, rcvTimeoutMs);
    }

    private static void DisposeGatewaySlots(IReadOnlyList<GatewayClientSlot> slots)
    {
        for (int i = 0; i < slots.Count; i++)
            TryDisposeQuietly(slots[i]);
    }

    private static void ConnectRegistryWithRetry(Action connect)
    {
        Exception? last = null;
        if (WaitUntil(() =>
            {
                try
                {
                    connect();
                    return true;
                }
                catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                                || IsInterrupted(ex.Errno))
                {
                    last = ex;
                    return false;
                }
            }, 5000, 20))
        {
            return;
        }

        if (last != null)
            throw last;
        throw new TimeoutException("registry connect timeout");
    }

    private readonly struct GatewayClientConfig
    {
        internal GatewayClientConfig(string transport, int size, int warmupSeconds,
            int durationSeconds, int settleMs, int drainMs, int sizeTransitionDrainMs,
            int sndTimeoutMs, int rcvTimeoutMs, int readyTimeoutMs,
            int clientPollTimeoutMs, int latencySampleCap, int clientCount)
        {
            Transport = transport;
            Size = size;
            WarmupSeconds = warmupSeconds;
            DurationSeconds = durationSeconds;
            SettleMs = settleMs;
            DrainMs = drainMs;
            SizeTransitionDrainMs = sizeTransitionDrainMs;
            SndTimeoutMs = sndTimeoutMs;
            RcvTimeoutMs = rcvTimeoutMs;
            ReadyTimeoutMs = readyTimeoutMs;
            ClientPollTimeoutMs = clientPollTimeoutMs;
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
        internal int SndTimeoutMs { get; }
        internal int RcvTimeoutMs { get; }
        internal int ReadyTimeoutMs { get; }
        internal int ClientPollTimeoutMs { get; }
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

        internal static GatewayClientResult Empty =>
            new(0.0, 0.0, 0.0, 0.0);
    }

    private readonly struct GatewayClientActiveStats
    {
        internal GatewayClientActiveStats(long measureCount,
            long benchStartTicks, long benchEndTicks, bool hasValue)
        {
            MeasureCount = measureCount;
            BenchStartTicks = benchStartTicks;
            BenchEndTicks = benchEndTicks;
            HasValue = hasValue;
        }

        internal long MeasureCount { get; }
        internal long BenchStartTicks { get; }
        internal long BenchEndTicks { get; }
        internal bool HasValue { get; }
    }

    private struct GatewayClientPhaseState
    {
        internal GatewayClientPhaseState(int latencySampleCap)
        {
            Seq = 1;
            SampleSeen = 0;
            Rng = 0xA341316Cu;
            LatencySampleCap = latencySampleCap;
            LatencySamples = new List<double>(latencySampleCap);
            AwaitingReply = Array.Empty<byte>();
            RoundRobinIndex = 0;
        }

        internal ulong Seq;
        internal long SampleSeen;
        internal uint Rng;
        internal int LatencySampleCap;
        internal List<double> LatencySamples;
        internal byte[] AwaitingReply;
        internal int RoundRobinIndex;

        internal void EnsureSlotState(int slotCount)
        {
            if (AwaitingReply == null || AwaitingReply.Length != slotCount)
                AwaitingReply = new byte[slotCount];
            else
                Array.Clear(AwaitingReply, 0, AwaitingReply.Length);
        }
    }

    private enum GatewayPhaseSendResult
    {
        Pending = 0,
        Progressed = 1,
        Fatal = 2,
    }

    private static bool PrimeRoundtripAllSlots(
        IReadOnlyList<GatewayClientSlot> slots, GatewayClientConfig config)
    {
        if (slots.Count == 0)
            return false;

        long deadlineTicks = Stopwatch.GetTimestamp()
            + MillisecondsToTicks(Math.Max(1000, config.ReadyTimeoutMs * 4));
        byte[] awaitingReply = new byte[slots.Count];
        byte[] roundtripCount = new byte[slots.Count];
        byte[] sendPending = new byte[slots.Count];
        int primedCount = 0;
        int roundRobinIndex = 0;
        using var receiverPoller = new Poller();
        using var sendPoller = new Poller();
        var receiverEvents = new PollEvent[Math.Max(1, slots.Count)];
        var sendEvents = new PollEvent[Math.Max(1, slots.Count)];
        for (int i = 0; i < slots.Count; i++)
        {
            receiverPoller.AddReceiver(slots[i].Receiver, PollEvents.PollIn, i);
            sendPoller.AddGateway(slots[i].Gateway, PollEvents.None, i);
        }

        while (primedCount < slots.Count
               && Stopwatch.GetTimestamp() < deadlineTicks)
        {
            int start = roundRobinIndex;
            for (int attempts = 0; attempts < slots.Count; attempts++)
            {
                int index = (start + attempts) % slots.Count;
                if (!CanStartPrimeRoundtrip(roundtripCount, awaitingReply,
                        sendPending, index, slots.Count, 1))
                {
                    continue;
                }

                if (!TryPrimeSend(slots, sendPoller, sendPending, awaitingReply,
                        roundtripCount, index))
                {
                    return false;
                }
            }
            roundRobinIndex = (start + 1) % slots.Count;

            int written = receiverPoller.Wait(receiverEvents.AsSpan(),
                Math.Max(0, config.ClientPollTimeoutMs), out int readyCount);
            if (readyCount > 0)
            {
                for (int eventIndex = 0; eventIndex < written; eventIndex++)
                {
                    if (receiverEvents[eventIndex].Tag is not int index)
                        continue;

                    if (!TryReceiveGatewayEcho(slots[index], out int n))
                        continue;
                    if (n <= 0)
                        continue;

                    awaitingReply[index] = 0;
                    if (roundtripCount[index] < 1)
                    {
                        roundtripCount[index]++;
                        if (roundtripCount[index] == 1)
                            primedCount++;
                    }
                }
            }

            written = sendPoller.Wait(sendEvents.AsSpan(), 0, out readyCount);
            if (readyCount > 0)
            {
                for (int eventIndex = 0; eventIndex < written; eventIndex++)
                {
                    if (sendEvents[eventIndex].Tag is not int index)
                        continue;
                    if (!CanResumePrimeRoundtrip(roundtripCount, awaitingReply,
                            sendPending, index, slots.Count, 1))
                    {
                        continue;
                    }

                    if (!TryPrimeSend(slots, sendPoller, sendPending,
                            awaitingReply, roundtripCount, index))
                    {
                        return false;
                    }
                }
            }
        }

        return primedCount == slots.Count;
    }

    private static bool DrainPendingReplies(
        IReadOnlyList<GatewayClientSlot> activeSlots, GatewayClientConfig config,
        ref GatewayClientPhaseState state)
    {
        if (PendingReplyCount(state) <= 0)
            return true;

        long settleDeadlineTicks = Stopwatch.GetTimestamp()
            + MillisecondsToTicks(Math.Max(config.ReadyTimeoutMs,
                Math.Max(100, config.SettleMs)));
        while (PendingReplyCount(state) > 0)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (nowTicks >= settleDeadlineTicks)
                return false;

            long sliceDeadlineTicks = Math.Min(settleDeadlineTicks,
                nowTicks + MillisecondsToTicks(50));
            _ = RunEchoPhaseLoop(activeSlots, config.Size, RunId,
                PerfPhase.Warmup, sliceDeadlineTicks, allowSend: false,
                collectMetrics: false, config, ref state);
        }

        return true;
    }

    private static int PendingReplyCount(GatewayClientPhaseState state)
    {
        if (state.AwaitingReply == null)
            return 0;

        int count = 0;
        for (int i = 0; i < state.AwaitingReply.Length; i++)
        {
            if (state.AwaitingReply[i] != 0)
                count++;
        }

        return count;
    }

    private static bool TryPrimeSend(IReadOnlyList<GatewayClientSlot> slots,
        Poller sendPoller, byte[] sendPending, byte[] awaitingReply,
        byte[] roundtripCount, int index)
    {
        GatewayClientSlot slot = slots[index];
        if (roundtripCount[index] >= 1 || awaitingReply[index] != 0)
            return true;

        if (TrySendGatewayPayload(slot, slot.Payload.AsSpan()))
        {
            sendPending[index] = 0;
            sendPoller.ModifyGateway(slot.Gateway, PollEvents.None);
            awaitingReply[index] = 1;
            return true;
        }

        sendPending[index] = 1;
        sendPoller.ModifyGateway(slot.Gateway, PollEvents.PollOut);
        return true;
    }

    private static bool CanStartGatewaySend(GatewayClientPhaseState state,
        byte[] sendPending, int index, int slotCount)
    {
        return index < slotCount && state.AwaitingReply[index] == 0
               && sendPending[index] == 0;
    }

    private static bool CanResumeGatewaySend(GatewayClientPhaseState state,
        byte[] sendPending, int index, int slotCount)
    {
        return index < slotCount && state.AwaitingReply[index] == 0
               && sendPending[index] != 0;
    }

    private static bool CanStartPrimeRoundtrip(byte[] roundtripCount,
        byte[] awaitingReply, byte[] sendPending, int index, int slotCount,
        byte targetRoundtrips)
    {
        return index < slotCount && roundtripCount[index] < targetRoundtrips
               && awaitingReply[index] == 0 && sendPending[index] == 0;
    }

    private static bool CanResumePrimeRoundtrip(byte[] roundtripCount,
        byte[] awaitingReply, byte[] sendPending, int index, int slotCount,
        byte targetRoundtrips)
    {
        return index < slotCount && roundtripCount[index] < targetRoundtrips
               && awaitingReply[index] == 0 && sendPending[index] != 0;
    }

    private static long MillisecondsToTicks(int durationMs)
    {
        long ticks = (long)Math.Max(0, durationMs) * Stopwatch.Frequency / 1000;
        return ticks > 0 ? ticks : 1;
    }

    private sealed class GatewayClientSlot : IDisposable
    {
        internal GatewayClientSlot(Receiver receiver, Gateway gateway,
            Gateway.PreparedService serverService, string? serverRoutingId,
            int recvCapacity)
        {
            Receiver = receiver;
            Gateway = gateway;
            ServerService = serverService;
            ServerRoutingId = serverRoutingId;
            Payload = new byte[Math.Max(PerfMetricHeaderSize, recvCapacity)];
            Recv = new byte[Math.Max(256, recvCapacity)];
            Array.Fill(Payload, (byte)'a');
        }

        internal Receiver Receiver { get; }
        internal Gateway Gateway { get; }
        internal Gateway.PreparedService ServerService { get; }
        internal string? ServerRoutingId { get; set; }
        internal byte[] Payload { get; }
        internal byte[] Recv { get; }

        public void Dispose()
        {
            TryDisposeQuietly(Gateway);
            TryDisposeQuietly(Receiver);
        }
    }
}
