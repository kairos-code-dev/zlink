using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfSpotClient
{
    private const string Pattern = "SPOT";
    private const int SubscribeSettleMs = 300;
    private const uint ExpectedRunId = 1;

    internal static int Run(string transport, int size, string endpoint)
    {
        SpotClientConfig config = BuildConfig(transport, size);

        using var ctx = new Context();
        ApplyMultiClientContextOptions(ctx);
        var nodes = new List<SpotNode>(config.ClientCount);
        var subscribers = new List<Spot>(config.ClientCount);
        try
        {
            for (int i = 0; i < config.ClientCount; i++)
            {
                var node = new SpotNode(ctx);
                ConfigureSpotTlsSubscriberIfNeeded(node, config.Transport);
                node.ConnectPeerPub(endpoint);
                var subscriber = new Spot(node);
                subscriber.Subscribe("bench");
                nodes.Add(node);
                subscribers.Add(subscriber);
            }

            Thread.Sleep(SubscribeSettleMs);
            List<Spot> activeSubscribers = subscribers;
            if (activeSubscribers.Count == 0)
            {
                Console.Error.WriteLine("multi_client_error:no_ready_connections");
                return 2;
            }

            var recv = new byte[Math.Max(256, config.Size)];
            var phaseState = new SpotClientPhaseState(config.LatencySampleCap);
            RunWarmupPhase(activeSubscribers, recv, config,
                ref phaseState);
            RunSettlePhase(config);
            SpotClientActiveStats activeStats = RunActivePhase(activeSubscribers,
                recv, config, ref phaseState);
            RunDrainPhase(config);
            SpotClientResult result = ComputeResult(activeStats,
                phaseState.LatencySamples);

            PrintResult(Pattern, config.Transport, config.Size, result.Throughput,
                result.LatencyUs, result.LatencyP95Us, result.LatencyP99Us);
            return 0;
        }
        finally
        {
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
            warmupSeconds,
            ResolveMultiDurationSeconds(),
            ResolveMultiSettleMs(),
            drainMs,
            ResolveMultiSizeTransitionDrainMs(),
            ResolveMultiActiveWarmup(),
            ResolveMultiWarmupDrainMs(drainMs),
            ResolveMultiLatencySampleCap(),
            ResolveMultiClients(Pattern));
    }

    private static void RunWarmupPhase(List<Spot> activeSubscribers,
        byte[] recv, SpotClientConfig config,
        ref SpotClientPhaseState state)
    {
        _ = config.ActiveWarmup;
        if (config.WarmupSeconds <= 0)
            return;

        using var poller = new Poller();
        var events = new List<PollEvent>(activeSubscribers.Count);
        for (int i = 0; i < activeSubscribers.Count; i++)
            poller.AddSpotSub(activeSubscribers[i], PollEvents.PollIn, i);

        long warmupDeadlineTicks = Stopwatch.GetTimestamp()
            + (long)Math.Max(0, config.WarmupSeconds) * Stopwatch.Frequency;
        while (Stopwatch.GetTimestamp() < warmupDeadlineTicks)
        {
            if (!WaitForEvents(poller, events,
                    RemainingMilliseconds(warmupDeadlineTicks)))
            {
                continue;
            }

            for (int i = 0; i < events.Count; i++)
            {
                if (events[i].Tag is not int socketIndex)
                    continue;
                DrainSpotSubscriber(activeSubscribers[socketIndex],
                    recv.AsSpan(), static _ => true);
            }
        }

        if (config.WarmupDrainMs > 0)
            Thread.Sleep(config.WarmupDrainMs);
    }

    private static void RunSettlePhase(SpotClientConfig config)
    {
        if (config.SettleMs > 0)
            Thread.Sleep(config.SettleMs);
    }

    private static SpotClientActiveStats RunActivePhase(
        List<Spot> activeSubscribers, byte[] recv,
        SpotClientConfig config, ref SpotClientPhaseState state)
    {
        List<double> latencySamples = state.LatencySamples;
        long sampleSeen = state.SampleSeen;
        uint rng = state.Rng;
        long measureCount = 0;
        long loopStartTicks = Stopwatch.GetTimestamp();
        long benchStartTicks = 0;
        long benchEndTicks = 0;
        long benchDeadlineTicks = 0;
        long waitActiveDeadlineTicks = loopStartTicks
            + (long)Math.Max(6, config.DurationSeconds + config.WarmupSeconds + 2)
            * Stopwatch.Frequency;
        using var poller = new Poller();
        var events = new List<PollEvent>(activeSubscribers.Count);
        for (int i = 0; i < activeSubscribers.Count; i++)
            poller.AddSpotSub(activeSubscribers[i], PollEvents.PollIn, i);
        while (true)
        {
            long nowTicks = Stopwatch.GetTimestamp();
            if (benchStartTicks > 0)
            {
                if (nowTicks >= benchDeadlineTicks)
                    break;
            }
            else if (nowTicks >= waitActiveDeadlineTicks)
            {
                break;
            }

            if (!WaitForEvents(poller, events,
                    benchStartTicks > 0
                        ? RemainingMilliseconds(benchDeadlineTicks)
                        : RemainingMilliseconds(waitActiveDeadlineTicks)))
            {
                continue;
            }

            for (int i = 0; i < events.Count; i++)
            {
                if (events[i].Tag is not int socketIndex)
                    continue;

                DrainSpotSubscriber(activeSubscribers[socketIndex],
                    recv.AsSpan(), body =>
                {
                    long endTicks = Stopwatch.GetTimestamp();
                    bool headerOk = TryDecodeMetricHeader(body,
                        out PerfMetricHeader header);
                    if (headerOk
                        && header.RunId == ExpectedRunId
                        && header.MsgSize == (uint)config.Size
                        && header.Phase == (uint)PerfPhase.Active)
                    {
                        if (benchStartTicks == 0)
                        {
                            benchStartTicks = endTicks;
                            benchDeadlineTicks = benchStartTicks
                                + (long)Math.Max(1, config.DurationSeconds)
                                * Stopwatch.Frequency;
                        }

                        if (endTicks <= benchDeadlineTicks)
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

                    return true;
                });
            }
        }

        long loopEndTicks = Stopwatch.GetTimestamp();
        if (benchStartTicks == 0)
            benchStartTicks = loopStartTicks;
        if (benchEndTicks <= benchStartTicks)
            benchEndTicks = loopEndTicks;
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
        List<double> latencySamples)
    {
        double elapsedSeconds = (stats.BenchEndTicks - stats.BenchStartTicks)
            / (double)Stopwatch.Frequency;
        double throughput = elapsedSeconds > 0.0
            ? stats.MeasureCount / elapsedSeconds
            : 0.0;
        double fallbackLatencyUs = (elapsedSeconds * 1_000_000.0)
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

    private static int DrainSpotSubscriber(Spot subscriber, Span<byte> payloadBuffer,
        PayloadHandler onPayload)
    {
        int count = 0;
        while (true)
        {
            int n = ReceiveSpotPayload(subscriber, payloadBuffer);
            if (n <= 0)
                break;

            count++;
            if (!onPayload(payloadBuffer.Slice(0, n)))
                break;
        }

        return count;
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
        internal SpotClientConfig(string transport, int size, int warmupSeconds,
            int durationSeconds, int settleMs, int drainMs,
            int sizeTransitionDrainMs, bool activeWarmup, int warmupDrainMs,
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
        internal int LatencySampleCap { get; }
        internal int ClientCount { get; }
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
