using System;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfSpotServer
{
    private const string Pattern = "SPOT";
    private const uint RunId = 1;
    private const string ServiceName = "bench-svc";
    private const string Topic = "bench";
    internal static int Run(PerfOptions options)
    {
        SpotServerConfig config = BuildConfig(options);
        var control = new ControlState();

        using var ctx = new Context();
        ApplyMultiServerContextOptions(ctx, options);
        using var nodePub = new SpotNode(ctx);
        using var spotPub = new Spot(nodePub);

        ConfigureSpotTlsPublisherIfNeeded(nodePub, config.Transport);
        TryConfigureSpotPublisherSocket(nodePub, options, config.SndTimeoutMs,
            config.RcvTimeoutMs);
        nodePub.Bind(config.Endpoint);
        StartControlWatcher(control);

        Console.WriteLine($"READY,{config.Endpoint}");
        if (!WaitForStart(control, config.Size, config.ReadyTimeoutMs))
        {
            Console.Error.WriteLine("multi_server_error:no_start_signal");
            return 2;
        }

        var phaseState = new SpotServerPhaseState(config.Size);
        RunPhase(spotPub, control, config, PerfPhase.Warmup,
            config.WarmupSeconds, ref phaseState);
        SpotServerActiveStats activeStats = RunActivePhase(spotPub, control,
            config, ref phaseState);
        SpotServerResult result = ComputeResult(activeStats,
            config.DurationSeconds);

        PrintResult(Pattern, config.Transport, config.Size, result.Throughput,
            result.LatencyNs, result.LatencyP95Ns, result.LatencyP99Ns);
        return 0;
    }

    private static SpotServerConfig BuildConfig(PerfOptions options)
    {
        int resolvedSize = Math.Max(1, options.Size);
        int warmupSeconds = ResolveMultiWarmupSeconds(options);
        if (resolvedSize >= 65536 && warmupSeconds > 0)
            warmupSeconds = 0;

        return new SpotServerConfig(
            options.Transport,
            resolvedSize,
            warmupSeconds,
            ResolveMultiDurationSeconds(options),
            Math.Min(ResolveMultiSndTimeoutMs(options), 200),
            ResolveMultiRcvTimeoutMs(options),
            ResolveMultiConnectReadyTimeoutMs(options),
            ResolveMultiClients(options),
            MultiEndpointFor(options.Transport, "multi-spot", options));
    }

    private static void RunPhase(Spot spotPub, ControlState control,
        SpotServerConfig config, PerfPhase phase, int durationValue,
        ref SpotServerPhaseState state)
    {
        if (durationValue <= 0)
            return;

        long durationTicks = (long)Math.Max(0, durationValue)
            * Stopwatch.Frequency;
        long deadlineTicks = Stopwatch.GetTimestamp() + durationTicks;
        while (!control.StopRequested
               && Stopwatch.GetTimestamp() < deadlineTicks)
        {
            StampMetricHeader(state.Payload.AsSpan(), RunId, phase,
                config.Size, state.Seq++, EpochNs());
            if (!PublishUntilReady(spotPub, state.Payload.AsSpan(),
                    deadlineTicks, control))
            {
                break;
            }
        }
    }

    private static SpotServerActiveStats RunActivePhase(Spot spotPub,
        ControlState control, SpotServerConfig config,
        ref SpotServerPhaseState state)
    {
        long sendCount = 0;
        long benchStartTicks = Stopwatch.GetTimestamp();
        long benchDeadlineTicks = benchStartTicks
            + (long)Math.Max(1, config.DurationSeconds) * Stopwatch.Frequency;
        while (!control.StopRequested
               && Stopwatch.GetTimestamp() < benchDeadlineTicks)
        {
            StampMetricHeader(state.Payload.AsSpan(), RunId, PerfPhase.Active,
                config.Size, state.Seq++, EpochNs());
            if (PublishUntilReady(spotPub, state.Payload.AsSpan(),
                    benchDeadlineTicks, control))
                sendCount++;
            else
                break;
        }

        long benchEndTicks = Stopwatch.GetTimestamp();
        return new SpotServerActiveStats(sendCount, benchStartTicks,
            benchEndTicks);
    }

    private static SpotServerResult ComputeResult(SpotServerActiveStats stats,
        int durationSeconds)
    {
        double configuredSeconds = Math.Max(1.0, durationSeconds);
        double throughput = stats.SendCount / configuredSeconds;
        double latencyNs = (configuredSeconds * 1_000_000_000.0)
            / Math.Max(1.0, stats.SendCount);
        return new SpotServerResult(throughput, latencyNs, latencyNs, latencyNs);
    }

    private static bool PublishUntilReady(Spot spotPub, ReadOnlySpan<byte> payload,
        long deadlineTicks, ControlState control)
    {
        var spin = new SpinWait();
        while (true)
        {
            if (control.StopRequested)
                return false;
            if (Stopwatch.GetTimestamp() >= deadlineTicks)
                return false;

            using Message message = Message.FromBytes(payload);
            SendResult result = spotPub.TryPublish(ServiceName, Topic, message);
            if (result == SendResult.Sent)
                return true;

            spin.SpinOnce();
        }
    }

    private static void StartControlWatcher(ControlState control)
    {
        var thread = new Thread(static state =>
        {
            var watcher = (ControlState)state!;
            while (true)
            {
                string? line = Console.ReadLine();
                if (line == null)
                {
                    watcher.RequestStop();
                    return;
                }

                if (string.Equals(line, "STOP", StringComparison.OrdinalIgnoreCase)
                    || string.Equals(line, "QUIT",
                        StringComparison.OrdinalIgnoreCase))
                {
                    watcher.RequestStop();
                    return;
                }

                if (TryParseStart(line, out int startSize))
                    watcher.SetStartSize(startSize);
            }
        });
        thread.IsBackground = true;
        thread.Start(control);
    }

    private static bool TryParseStart(string line, out int msgSize)
    {
        msgSize = 0;
        const string prefix = "START,";
        if (!line.StartsWith(prefix, StringComparison.Ordinal))
            return false;
        return int.TryParse(line.AsSpan(prefix.Length), out msgSize)
               && msgSize > 0;
    }

    private static bool WaitForStart(ControlState control, int size, int timeoutMs)
    {
        long deadlineTicks = DeadlineTicksFromMilliseconds(timeoutMs);
        var spin = new SpinWait();
        while (Stopwatch.GetTimestamp() < deadlineTicks)
        {
            if (control.StopRequested)
                return false;
            if (Volatile.Read(ref control.StartSize) == size)
                return true;
            spin.SpinOnce();
        }

        return false;
    }

    private static void TryConfigureSpotPublisherSocket(SpotNode node,
        PerfOptions options, int sndTimeoutMs, int rcvTimeoutMs)
    {
        int sndHwm = options.ResolveMultiHwm("PERF_SNDHWM");
        int rcvHwm = options.ResolveMultiHwm("PERF_RCVHWM");
        int xpubNoDrop = options.SpotXpubNoDrop > 0 ? 1 : 0;
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

    private static void TrySetSpotNodeSocketOption(SpotNode node,
        SpotNodeSocketRole role, SocketOptionKey<int> option, int value)
    {
        try
        {
            node.SetOption(role, option, value);
        }
        catch (ZlinkException ex) when (ShouldIgnoreSpotOptionError(ex.InternalErrno))
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
            int durationSeconds, int sndTimeoutMs, int rcvTimeoutMs,
            int readyTimeoutMs, int clientCount, string endpoint)
        {
            Transport = transport;
            Size = size;
            WarmupSeconds = warmupSeconds;
            DurationSeconds = durationSeconds;
            SndTimeoutMs = sndTimeoutMs;
            RcvTimeoutMs = rcvTimeoutMs;
            ReadyTimeoutMs = readyTimeoutMs;
            ClientCount = clientCount;
            Endpoint = endpoint;
        }

        internal string Transport { get; }
        internal int Size { get; }
        internal int WarmupSeconds { get; }
        internal int DurationSeconds { get; }
        internal int SndTimeoutMs { get; }
        internal int RcvTimeoutMs { get; }
        internal int ReadyTimeoutMs { get; }
        internal int ClientCount { get; }
        internal string Endpoint { get; }
    }

    private readonly struct SpotServerResult
    {
        internal SpotServerResult(double throughput, double latencyNs,
            double latencyP95Ns, double latencyP99Ns)
        {
            Throughput = throughput;
            LatencyNs = latencyNs;
            LatencyP95Ns = latencyP95Ns;
            LatencyP99Ns = latencyP99Ns;
        }

        internal double Throughput { get; }
        internal double LatencyNs { get; }
        internal double LatencyP95Ns { get; }
        internal double LatencyP99Ns { get; }
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

    private sealed class ControlState
    {
        internal int StartSize;
        private int _stopFlag;

        internal bool StopRequested => Volatile.Read(ref _stopFlag) != 0;

        internal void SetStartSize(int size)
        {
            Volatile.Write(ref StartSize, size);
        }

        internal void RequestStop()
        {
            Volatile.Write(ref _stopFlag, 1);
        }
    }
}
