using System;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfSpotServer
{
    private const string Pattern = "SPOT";
    private const int SubscribeSettleMs = 300;
    private const uint RunId = 1;

    internal static int Run(string transport, int size)
    {
        SpotServerConfig config = BuildConfig(transport, size);

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx);
        using var nodePub = new SpotNode(ctx);
        ConfigureSpotTlsPublisherIfNeeded(nodePub, config.Transport);
        TryConfigureSpotPublisherSocket(nodePub, Pattern, config.SndTimeoutMs);
        nodePub.Bind(config.Endpoint);
        using var spotPub = new Spot(nodePub);

        Console.WriteLine($"READY,{config.Endpoint}");
        _ = WaitUntil(() => nodePub.GetSubPeers().Length > 0, config.ReadyTimeoutMs);
        Thread.Sleep(SubscribeSettleMs);

        var phaseState = new SpotServerPhaseState(config.Size);
        RunWarmupPhase(spotPub, config, ref phaseState);
        RunSettlePhase(config);
        SpotServerActiveStats activeStats = RunActivePhase(spotPub, config,
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
            ResolveMultiConnectReadyTimeoutMs(),
            MultiEndpointFor(transport, "multi-spot"));
    }

    private static void RunWarmupPhase(Spot spotPub,
        SpotServerConfig config,
        ref SpotServerPhaseState state)
    {
        if (config.WarmupSeconds <= 0)
            return;

        using var poller = new Poller();
        poller.AddSpotPub(spotPub, PollEvents.PollOut);
        long warmupDeadline = Stopwatch.GetTimestamp()
            + (long)config.WarmupSeconds * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < warmupDeadline)
        {
            StampMetricHeader(state.Payload.AsSpan(), RunId, PerfPhase.Warmup,
                config.Size, state.Seq++, EpochUs());
            PublishUntilReady(spotPub, poller, state.Payload.AsSpan());
        }
    }

    private static void RunSettlePhase(SpotServerConfig config)
    {
        if (config.SettleMs > 0)
            Thread.Sleep(config.SettleMs);
    }

    private static SpotServerActiveStats RunActivePhase(Spot spotPub,
        SpotServerConfig config, ref SpotServerPhaseState state)
    {
        long sendCount = 0;
        using var poller = new Poller();
        poller.AddSpotPub(spotPub, PollEvents.PollOut);
        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, config.DurationSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            StampMetricHeader(state.Payload.AsSpan(), RunId, PerfPhase.Active,
                config.Size, state.Seq++, EpochUs());
            if (PublishUntilReady(spotPub, poller, state.Payload.AsSpan()))
                sendCount++;
        }

        long benchEndTicks = Stopwatch.GetTimestamp();
        return new SpotServerActiveStats(sendCount, benchStartTicks,
            benchEndTicks);
    }

    private static SpotServerResult ComputeResult(SpotServerActiveStats stats)
    {
        double elapsedSeconds = (stats.BenchEndTicks - stats.BenchStartTicks)
            / (double)Stopwatch.Frequency;
        double throughput = elapsedSeconds > 0.0
            ? stats.SendCount / elapsedSeconds
            : 0.0;
        double latencyUs = (elapsedSeconds * 1_000_000.0)
            / Math.Max(1.0, stats.SendCount);
        return new SpotServerResult(throughput, latencyUs, latencyUs, latencyUs);
    }

    private static bool PublishUntilReady(Spot spotPub, Poller poller,
        ReadOnlySpan<byte> payload)
    {
        var events = new List<PollEvent>(1);
        while (true)
        {
            if (!WaitForEvents(poller, events, 100))
                continue;

            try
            {
                spotPub.Publish("bench", payload, SendFlags.None);
                return true;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                            || IsInterrupted(ex.Errno))
            {
                continue;
            }
        }
    }

    private static void TryConfigureSpotPublisherSocket(SpotNode node,
        string pattern, int sndTimeoutMs)
    {
        int sndHwm = ResolveMultiHwmValue("PERF_SNDHWM", pattern);
        TrySetSpotNodeOption(node, SpotNodeOption.PubMode,
            (int)SpotNodePubMode.Sync);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.Blocky, 0);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.Linger, 0);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.SndHwm, sndHwm);
        TrySetSpotNodeSocketOption(node, SpotNodeSocketRole.Pub,
            SocketOptions.SndTimeo, sndTimeoutMs);
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
            int durationSeconds, int settleMs, int sndTimeoutMs, int readyTimeoutMs,
            string endpoint)
        {
            Transport = transport;
            Size = size;
            WarmupSeconds = warmupSeconds;
            DurationSeconds = durationSeconds;
            SettleMs = settleMs;
            SndTimeoutMs = sndTimeoutMs;
            ReadyTimeoutMs = readyTimeoutMs;
            Endpoint = endpoint;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int WarmupSeconds { get; }
        internal int DurationSeconds { get; }
        internal int SettleMs { get; }
        internal int SndTimeoutMs { get; }
        internal int ReadyTimeoutMs { get; }
        internal string Endpoint { get; }
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
