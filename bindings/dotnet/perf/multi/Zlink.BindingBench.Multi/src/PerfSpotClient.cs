using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using Zlink.Service;
using static PerfRunner;

internal static class PerfSpotClient
{
    private const string Pattern = "SPOT";
    private const string ServiceName = "perf-spot";
    private const uint ExpectedRunId = 1;

    internal static int Run(string transport, int size, string endpoint)
    {
        SpotClientConfig config = BuildConfig(transport, size);

        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx);
        var nodes = new List<SpotNode>(config.ClientCount);
        var subscribers = new List<Spot>(config.ClientCount);
        Discovery? discovery = null;
        try
        {
            if (!TryParseSpotReadyEndpoint(endpoint, out _, out _,
                    out string registryRouter))
            {
                Console.Error.WriteLine("multi_client_error:invalid_spot_ready_payload");
                return 1;
            }

            discovery = new Discovery(ctx, DiscoveryServiceType.Spot);
            ConnectRegistryWithRetry(() => discovery.ConnectRegistry(
                registryRouter));
            if (!WaitUntil(() => discovery.ServiceAvailable(ServiceName)
                    || discovery.ReceiverCount(ServiceName) > 0,
                    Math.Max(5000, config.ConnectReadyTimeoutMs)))
            {
                Console.Error.WriteLine(
                    "multi_client_error:spot_discovery_not_ready");
                return 2;
            }

            for (int i = 0; i < config.ClientCount; i++)
            {
                var node = new SpotNode(ctx);
                ConfigureSpotTlsSubscriberIfNeeded(node, config.Transport);
                ApplySpotNodeSubscriberOptions(node, config);
                node.Bind(EndpointFor(transport, $"multi-spot-client-{i}"));
                node.SetDiscovery(discovery, ServiceName);
                var subscriber = new Spot(node);
                subscriber.Subscribe("bench");
                node.SetDiscovery(discovery, ServiceName);
                nodes.Add(node);
                subscribers.Add(subscriber);
            }

            List<Spot> activeSubscribers = subscribers;
            if (activeSubscribers.Count == 0)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }
            if (!WaitSubPeers(nodes, config.ConnectReadyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:no_sub_peers");
                return 2;
            }
            if (config.SettleMs > 0)
                Thread.Sleep(config.SettleMs);

            var recv = new byte[Math.Max(256, config.Size)];
            var phaseState = new SpotClientPhaseState(config.LatencySampleCap);
            if (!WaitForMsgSizeStart(activeSubscribers, recv, config.Size,
                    config.ConnectReadyTimeoutMs))
            {
                Console.Error.WriteLine("multi_client_error:no_msg_size_start");
                return 2;
            }
            SpotClientActiveStats activeStats = RunActivePhase(activeSubscribers,
                recv, config, ref phaseState);
            RunDrainPhase(config);
            SpotClientResult result = ComputeResult(activeStats,
                phaseState.LatencySamples, config.DurationSeconds);

            PrintResult(Pattern, config.Transport, config.Size, result.Throughput,
                result.LatencyUs, result.LatencyP95Us, result.LatencyP99Us);
            Console.Out.Flush();
            Console.Error.Flush();
            Environment.Exit(0);
            return 0;
        }
        finally
        {
            TryDisposeQuietly(discovery);
            DisposeAllQuietly(nodes);
            DisposeAllQuietly(subscribers);
        }
    }

    private static SpotClientConfig BuildConfig(string transport, int size)
    {
        int resolvedSize = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        if (resolvedSize >= 65536 && warmupSeconds > 0)
            warmupSeconds = 0;
        int drainMs = ResolveMultiDrainMs(Pattern);

        return new SpotClientConfig(
            transport,
            resolvedSize,
            ResolveMultiDurationSeconds(),
            ResolveMultiSettleMs(),
            drainMs,
            ResolveMultiSizeTransitionDrainMs(),
            ResolveMultiLatencySampleCap(),
            ResolveMultiClients(Pattern),
            ResolveMultiConnectReadyTimeoutMs());
    }

    private static SpotClientActiveStats RunActivePhase(
        List<Spot> activeSubscribers, byte[] recv,
        SpotClientConfig config, ref SpotClientPhaseState state)
    {
        List<double> latencySamples = state.LatencySamples;
        long sampleSeen = state.SampleSeen;
        uint rng = state.Rng;
        long measureCount = 0;
        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchEndTicks = benchStartTicks;
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, config.DurationSeconds) * Stopwatch.Frequency;
        using var poller = new Poller();
        var events = new PollEvent[Math.Max(1, activeSubscribers.Count)];
        for (int i = 0; i < activeSubscribers.Count; i++)
            poller.AddSpotSub(activeSubscribers[i], PollEvents.PollIn, i);
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            int written = poller.Wait(events.AsSpan(),
                RemainingMilliseconds(benchDeadlineTicks), out int totalReady);
            if (totalReady <= 0)
            {
                continue;
            }

            for (int i = 0; i < written; i++)
            {
                if (events[i].Tag is not int socketIndex)
                    continue;

                while (true)
                {
                    int n = ReceiveSpotPayload(activeSubscribers[socketIndex],
                        recv.AsSpan());
                    if (n <= 0)
                        break;

                    long endTicks = Stopwatch.GetTimestamp();
                    if (endTicks >= benchDeadlineTicks)
                        break;

                    if (TryDecodeMetricHeader(recv.AsSpan(0, n),
                            out PerfMetricHeader header)
                        && header.RunId == ExpectedRunId
                        && header.MsgSize == (uint)config.Size
                        && (header.Phase == (uint)PerfPhase.Active
                            || header.Phase == (uint)PerfPhase.Drain
                            || header.Phase == (uint)PerfPhase.Warmup))
                    {
                        measureCount++;
                        benchEndTicks = endTicks;
                        ulong nowUs = EpochUs();
                        if (header.SentTsUs > 0 && nowUs >= header.SentTsUs)
                        {
                            double sampleLatencyUs = nowUs - header.SentTsUs;
                            ReservoirSample(latencySamples, sampleLatencyUs,
                                ref sampleSeen, config.LatencySampleCap,
                                ref rng);
                        }
                    }
                }
            }
        }

        if (benchEndTicks <= benchStartTicks)
            benchEndTicks = Stopwatch.GetTimestamp();
        state.SampleSeen = sampleSeen;
        state.Rng = rng;

        return new SpotClientActiveStats(measureCount, benchStartTicks,
            benchEndTicks);
    }

    private static void RunDrainPhase(SpotClientConfig config)
    {
        if (config.DrainMs > 0)
            Thread.Sleep(config.DrainMs);
        if (config.SizeTransitionDrainMs > 0)
            Thread.Sleep(config.SizeTransitionDrainMs);
    }

    private static SpotClientResult ComputeResult(SpotClientActiveStats stats,
        List<double> latencySamples, int durationSeconds)
    {
        double measuredSeconds = (stats.BenchEndTicks - stats.BenchStartTicks)
            / (double)Stopwatch.Frequency;
        double activeSeconds = Math.Max(1.0, durationSeconds);
        double throughput = stats.MeasureCount / activeSeconds;
        double fallbackLatencyUs = (measuredSeconds * 1_000_000.0)
            / Math.Max(1.0, stats.MeasureCount);
        var latency = ComputeLatencyStats(latencySamples);
        double latencyUs = latency.mean > 0.0 ? latency.mean : fallbackLatencyUs;
        double latencyP95Us = latency.p95 > 0.0 ? latency.p95 : latencyUs;
        double latencyP99Us = latency.p99 > 0.0 ? latency.p99 : latencyP95Us;
        return new SpotClientResult(throughput, latencyUs, latencyP95Us,
            latencyP99Us);
    }

    private static int ReceiveSpotPayload(Spot subscriber,
        Span<byte> payloadBuffer)
    {
        try
        {
            return subscriber.ReceiveSinglePayload(payloadBuffer,
                ReceiveFlags.DontWait);
        }
        catch (ZlinkException ex) when (IsWouldBlock(ex.Errno))
        {
            return 0;
        }
    }

    private static bool WaitSubPeers(List<SpotNode> nodes, int timeoutMs)
    {
        if (nodes.Count == 0)
            return false;

        long deadlineTicks = DeadlineTicksFromMilliseconds(
            Math.Max(5000, timeoutMs * 3));
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            int ready = 0;
            for (int i = 0; i < nodes.Count; i++)
            {
                try
                {
                    if (nodes[i].GetSubPeers().Length > 0)
                        ready++;
                }
                catch (ZlinkException)
                {
                    return false;
                }
            }

            if (ready == nodes.Count)
                return true;
            Thread.Sleep(1);
        }

        return false;
    }

    private static bool WaitForMsgSizeStart(List<Spot> subscribers,
        byte[] recv, int msgSize, int timeoutMs)
    {
        using var poller = new Poller();
        var events = new PollEvent[Math.Max(1, subscribers.Count)];
        for (int i = 0; i < subscribers.Count; i++)
            poller.AddSpotSub(subscribers[i], PollEvents.PollIn, i);

        long deadlineTicks = DeadlineTicksFromMilliseconds(Math.Max(5000,
            timeoutMs));
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            int written = poller.Wait(events.AsSpan(),
                RemainingMilliseconds(deadlineTicks), out int totalReady);
            if (totalReady <= 0)
            {
                continue;
            }

            for (int i = 0; i < written; i++)
            {
                if (events[i].Tag is not int socketIndex)
                    continue;

                while (true)
                {
                    int n = ReceiveSpotPayload(subscribers[socketIndex],
                        recv.AsSpan());
                    if (n <= 0)
                        break;

                    if (!TryDecodeMetricHeader(recv.AsSpan(0, n),
                            out PerfMetricHeader header))
                    {
                        continue;
                    }

                    if (header.RunId == ExpectedRunId
                        && header.MsgSize == (uint)msgSize)
                    {
                        return true;
                    }
                }
            }
        }

        return false;
    }

    private static void ApplySpotNodeSubscriberOptions(SpotNode node,
        SpotClientConfig config)
    {
        _ = config;
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", Pattern);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", Pattern);
        int rcvTimeo = ResolveMultiRcvTimeoutMs();
        int xpubNoDrop = ParsePositiveEnv("PERF_MULTI_SPOT_XPUB_NODROP", 1) > 0
            ? 1 : 0;
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.RcvHwm, rcvHwm);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.RcvTimeo, rcvTimeo);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.XPubNoDrop, xpubNoDrop);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.Linger, 0);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.Linger, 0);
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

    private static void TrySetSpotNodeSocketOption(SpotNode node,
        SpotNodeSocketRole role, SocketOptionKey<int> option, int value)
    {
        try
        {
            node.SetOption(role, option, value);
        }
        catch (ZlinkException ex) when (ShouldIgnoreSpotOptionError(ex.Errno))
        {
        }
    }

    private static bool ShouldIgnoreSpotOptionError(int errno)
    {
        ErrorCode code = ZlinkException.MapErrorCode(errno);
        return code == ErrorCode.ENotSup
               || code == ErrorCode.EInval
               || code == ErrorCode.EProtoNoSupport;
    }

    private static bool TryParseSpotReadyEndpoint(string endpoint,
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

    private static int RemainingMilliseconds(long deadlineTicks)
    {
        long nowTicks = Stopwatch.GetTimestamp();
        if (deadlineTicks <= nowTicks)
            return 0;

        double remainingMs = (deadlineTicks - nowTicks) * 1000.0
            / Stopwatch.Frequency;
        if (remainingMs >= int.MaxValue)
            return int.MaxValue;
        return (int)Math.Ceiling(remainingMs);
    }

    private readonly struct SpotClientConfig
    {
        internal SpotClientConfig(string transport, int size,
            int durationSeconds, int settleMs, int drainMs,
            int sizeTransitionDrainMs, int latencySampleCap, int clientCount,
            int connectReadyTimeoutMs)
        {
            Transport = transport;
            Size = size;
            DurationSeconds = durationSeconds;
            SettleMs = settleMs;
            DrainMs = drainMs;
            SizeTransitionDrainMs = sizeTransitionDrainMs;
            LatencySampleCap = latencySampleCap;
            ClientCount = clientCount;
            ConnectReadyTimeoutMs = connectReadyTimeoutMs;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int DurationSeconds { get; }
        internal int SettleMs { get; }
        internal int DrainMs { get; }
        internal int SizeTransitionDrainMs { get; }
        internal int LatencySampleCap { get; }
        internal int ClientCount { get; }
        internal int ConnectReadyTimeoutMs { get; }
    }

    private readonly struct SpotClientResult
    {
        internal SpotClientResult(double throughput, double latencyUs,
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

    private readonly struct SpotClientActiveStats
    {
        internal SpotClientActiveStats(long measureCount, long benchStartTicks,
            long benchEndTicks)
        {
            MeasureCount = measureCount;
            BenchStartTicks = benchStartTicks;
            BenchEndTicks = benchEndTicks;
        }

        internal long MeasureCount { get; }
        internal long BenchStartTicks { get; }
        internal long BenchEndTicks { get; }
    }

    private struct SpotClientPhaseState
    {
        internal SpotClientPhaseState(int latencySampleCap)
        {
            Index = 0;
            LatencySamples = new List<double>(latencySampleCap);
            SampleSeen = 0;
            Rng = 0xA341316Cu;
        }

        internal int Index;
        internal List<double> LatencySamples;
        internal long SampleSeen;
        internal uint Rng;
    }
}
