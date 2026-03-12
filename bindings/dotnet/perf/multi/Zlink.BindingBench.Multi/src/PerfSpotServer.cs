using System;
using System.Diagnostics;
using System.Threading;
using Zlink;
using Zlink.Service;
using static PerfRunner;

internal static class PerfSpotServer
{
    private const string Pattern = "SPOT";
    private const string ServiceName = "perf-spot";
    private const uint RunId = 1;

    internal static int Run(string transport, int size)
    {
        SpotServerConfig config = BuildConfig(transport, size);

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var registry = new Registry(ctx);
        using var discovery = new Discovery(ctx, DiscoveryServiceType.Spot);
        using var nodePub = new SpotNode(ctx);

        ApplyRegistrySocketOptions(registry, Pattern, config.SndTimeoutMs,
            config.RcvTimeoutMs);
        registry.SetHeartbeat(5000, 120000);
        registry.SetEndpoints(config.RegistryPub, config.RegistryRouter);
        registry.SetBroadcastInterval((uint)Math.Max(100, config.SettleMs));
        registry.Start();

        discovery.ConnectRegistry(config.RegistryRouter);
        ConfigureSpotTlsPublisherIfNeeded(nodePub, config.Transport);
        TryConfigureSpotPublisherSocket(nodePub, Pattern, config.SndTimeoutMs,
            config.RcvTimeoutMs);
        nodePub.SetDiscovery(discovery, ServiceName);
        nodePub.Bind(config.Endpoint);
        nodePub.Register(ServiceName, config.Endpoint);

        using var spotPub = new Spot(nodePub);
        PreparedTopic topic = spotPub.PrepareTopic("bench");
        Console.WriteLine(
            $"READY,{config.Endpoint}|{config.RegistryPub}|{config.RegistryRouter}");
        _ = WaitUntil(() => nodePub.GetPubPeers().Length >= config.ClientCount,
            config.ReadyTimeoutMs);

        var phaseState = new SpotServerPhaseState(config.Size);
        RunPhase(spotPub, topic, config, PerfPhase.Warmup, config.WarmupSeconds,
            ref phaseState);
        RunPhase(spotPub, topic, config, PerfPhase.Drain, config.SettleMs,
            ref phaseState);
        SpotServerActiveStats activeStats = RunActivePhase(spotPub, topic, config,
            ref phaseState);
        SpotServerResult result = ComputeResult(activeStats);

        PrintResult(Pattern, config.Transport, config.Size, result.Throughput,
            result.LatencyUs, result.LatencyP95Us, result.LatencyP99Us);
        return 0;
    }

    private static SpotServerConfig BuildConfig(string transport, int size)
    {
        int resolvedSize = Math.Max(1, size);
        int warmupSeconds = ResolveMultiWarmupSeconds();
        if (resolvedSize >= 65536 && warmupSeconds > 0)
            warmupSeconds = 0;

        return new SpotServerConfig(
            transport,
            resolvedSize,
            warmupSeconds,
            ResolveMultiDurationSeconds(),
            ResolveMultiSettleMs(),
            Math.Min(ResolveMultiSndTimeoutMs(), 200),
            ResolveMultiRcvTimeoutMs(),
            ResolveMultiConnectReadyTimeoutMs(),
            ResolveMultiClients(Pattern),
            MultiEndpointFor(transport, "multi-spot"),
            EndpointFor("tcp", "multi-spot-reg-pub"),
            EndpointFor("tcp", "multi-spot-reg-router"));
    }

    private static void RunPhase(Spot spotPub, PreparedTopic topic,
        SpotServerConfig config,
        PerfPhase phase, int durationValue, ref SpotServerPhaseState state)
    {
        if (durationValue <= 0)
            return;

        using var poller = new Poller();
        poller.AddSpotPub(spotPub, PollEvents.None);
        long durationTicks = phase == PerfPhase.Drain
            ? Math.Max(0, durationValue) * Stopwatch.Frequency / 1000
            : (long)Math.Max(0, durationValue) * Stopwatch.Frequency;
        long deadlineTicks = Stopwatch.GetTimestamp() + durationTicks;
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            StampMetricHeader(state.Payload.AsSpan(), RunId, phase,
                config.Size, state.Seq++, EpochUs());
            if (!PublishUntilReady(spotPub, topic, poller, state.Payload.AsSpan(),
                    deadlineTicks))
            {
                break;
            }
        }
    }

    private static SpotServerActiveStats RunActivePhase(Spot spotPub,
        PreparedTopic topic, SpotServerConfig config,
        ref SpotServerPhaseState state)
    {
        long sendCount = 0;
        using var poller = new Poller();
        poller.AddSpotPub(spotPub, PollEvents.None);
        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, config.DurationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            StampMetricHeader(state.Payload.AsSpan(), RunId, PerfPhase.Active,
                config.Size, state.Seq++, EpochUs());
            if (PublishUntilReady(spotPub, topic, poller, state.Payload.AsSpan(),
                    benchDeadlineTicks))
                sendCount++;
            else
                break;
        }

        long benchEndTicks = Stopwatch.GetTimestamp();
        return new SpotServerActiveStats(sendCount, benchStartTicks,
            benchEndTicks);
    }

    private static SpotServerResult ComputeResult(SpotServerActiveStats stats)
    {
        double configuredSeconds = Math.Max(1.0, ResolveMultiDurationSeconds());
        double throughput = stats.SendCount / configuredSeconds;
        double latencyUs = (configuredSeconds * 1_000_000.0)
            / Math.Max(1.0, stats.SendCount);
        return new SpotServerResult(throughput, latencyUs, latencyUs, latencyUs);
    }

    private static bool PublishUntilReady(Spot spotPub, PreparedTopic topic,
        Poller poller,
        ReadOnlySpan<byte> payload, long deadlineTicks)
    {
        var events = new List<PollEvent>(1);
        while (true)
        {
            if (Stopwatch.GetTimestamp() >= deadlineTicks)
                return false;
            try
            {
                spotPub.Publish(topic, payload, SendFlags.DontWait);
                poller.ModifySpotPub(spotPub, PollEvents.None);
                return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                            || IsInterrupted(ex.Errno))
            {
                poller.ModifySpotPub(spotPub, PollEvents.PollOut);
                if (!WaitForEvents(poller, events,
                        RemainingMilliseconds(deadlineTicks)))
                    continue;
                continue;
            }
        }
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

    private static void ApplyRegistrySocketOptions(Registry registry,
        string pattern, int sndTimeoutMs, int rcvTimeoutMs)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", pattern);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", pattern);
        registry.SetOption(RegistrySocketRole.Pub, SocketOptions.Linger, 0);
        registry.SetOption(RegistrySocketRole.Router, SocketOptions.Linger, 0);
        registry.SetOption(RegistrySocketRole.PeerSub, SocketOptions.Linger, 0);
        registry.SetOption(RegistrySocketRole.Pub, SocketOptions.SndHwm, sndHwm);
        registry.SetOption(RegistrySocketRole.Router, SocketOptions.SndHwm, sndHwm);
        registry.SetOption(RegistrySocketRole.Router, SocketOptions.RcvHwm, rcvHwm);
        registry.SetOption(RegistrySocketRole.PeerSub, SocketOptions.RcvHwm, rcvHwm);
        registry.SetOption(RegistrySocketRole.Pub, SocketOptions.SndTimeo,
            sndTimeoutMs);
        registry.SetOption(RegistrySocketRole.Router, SocketOptions.SndTimeo,
            sndTimeoutMs);
        registry.SetOption(RegistrySocketRole.Router, SocketOptions.RcvTimeo,
            rcvTimeoutMs);
        registry.SetOption(RegistrySocketRole.PeerSub, SocketOptions.RcvTimeo,
            rcvTimeoutMs);
    }

    private static void TryConfigureSpotPublisherSocket(SpotNode node,
        string pattern, int sndTimeoutMs, int rcvTimeoutMs)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", pattern);
        int rcvHwm = ResolveMultiHwmValue("PERF_RCVHWM", pattern);
        int xpubNoDrop = ParsePositiveEnv("PERF_MULTI_SPOT_XPUB_NODROP", 1) > 0
            ? 1 : 0;
        TrySetSpotNodeOption(node, SpotNodeOption.PubMode,
            (int)SpotNodePubMode.Sync);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.Linger, 0);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.SndHwm, sndHwm);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.SndTimeo, sndTimeoutMs);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.XPubNoDrop, xpubNoDrop);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.Linger, 0);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.RcvHwm, rcvHwm);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Sub,
            SocketOptions.RcvTimeo, rcvTimeoutMs);
    }

    private static void TrySetSpotNodeOption(SpotNode node, SpotNodeOption option,
        int value)
    {
        try
        {
            node.SetOption(option, value);
        }
        catch (ZlinkException ex) when (ShouldIgnoreSpotOptionError(ex.Errno))
        {
        }
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

    private readonly struct SpotServerConfig
    {
        internal SpotServerConfig(string transport, int size, int warmupSeconds,
            int durationSeconds, int settleMs, int sndTimeoutMs, int rcvTimeoutMs,
            int readyTimeoutMs, int clientCount, string endpoint,
            string registryPub, string registryRouter)
        {
            Transport = transport;
            Size = size;
            WarmupSeconds = warmupSeconds;
            DurationSeconds = durationSeconds;
            SettleMs = settleMs;
            SndTimeoutMs = sndTimeoutMs;
            RcvTimeoutMs = rcvTimeoutMs;
            ReadyTimeoutMs = readyTimeoutMs;
            ClientCount = clientCount;
            Endpoint = endpoint;
            RegistryPub = registryPub;
            RegistryRouter = registryRouter;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int WarmupSeconds { get; }
        internal int DurationSeconds { get; }
        internal int SettleMs { get; }
        internal int SndTimeoutMs { get; }
        internal int RcvTimeoutMs { get; }
        internal int ReadyTimeoutMs { get; }
        internal int ClientCount { get; }
        internal string Endpoint { get; }
        internal string RegistryPub { get; }
        internal string RegistryRouter { get; }
    }

    private readonly struct SpotServerResult
    {
        internal SpotServerResult(double throughput, double latencyUs,
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

    private readonly struct SpotServerActiveStats
    {
        internal SpotServerActiveStats(long sendCount, long benchStartTicks,
            long benchEndTicks)
        {
            SendCount = sendCount;
            BenchStartTicks = benchStartTicks;
            BenchEndTicks = benchEndTicks;
        }

        internal long SendCount { get; }
        internal long BenchStartTicks { get; }
        internal long BenchEndTicks { get; }
    }

    private struct SpotServerPhaseState
    {
        internal SpotServerPhaseState(int msgSize)
        {
            Seq = 1;
            Payload = new byte[Math.Max(msgSize, PerfMetricHeaderSize)];
            Array.Fill(Payload, (byte)'a');
        }

        internal ulong Seq;
        internal byte[] Payload;
    }
}
