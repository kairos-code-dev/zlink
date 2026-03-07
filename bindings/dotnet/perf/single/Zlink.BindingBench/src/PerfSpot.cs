using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfSpot
{
    internal static int RunSpot(string transport, int size)
    {
        int warmupCount = ResolveSingleWarmupCount("SPOT");
        if (size >= 65536 && warmupCount > 20)
            warmupCount = 20;
        int settleMs = SingleSettleTimeMs;
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount("SPOT");

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();
        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        SpotNode? nodePub = null;
        SpotNode? nodeSub = null;
        Spot? spotPub = null;
        Spot? spotSub = null;
        try
        {
            nodePub = new SpotNode(ctx);
            nodeSub = new SpotNode(ctx);
            string endpoint = EndpointFor(transport, "spot");
            ConfigureSpotTlsPublisherIfNeeded(nodePub, transport);
            ConfigureSpotTlsSubscriberIfNeeded(nodeSub, transport);
            nodePub.Bind(endpoint);
            nodeSub.ConnectPeerPub(endpoint);
            spotPub = new Spot(nodePub);
            spotSub = new Spot(nodeSub);
            spotSub.Subscribe("bench");
            Thread.Sleep(SingleConnectWaitMs);

            var payload = new byte[size];
            Array.Fill(payload, (byte)'a');
            var recvPayload = new byte[Math.Max(256, size)];

            for (int i = 0; i < warmupCount; i++)
            {
                spotPub.Publish("bench", payload.AsSpan(), SendFlags.None);
                ReceiveSpotPayloadBlockingAndDrain(spotSub, recvPayload.AsSpan());
            }

            Thread.Sleep(settleMs);

            int recvCount = 0;
            long benchStartTicks = Stopwatch.GetTimestamp();
            long throughputDeadlineTicks = DeadlineTicksFromSeconds(
                durationSeconds);
            while (Stopwatch.GetTimestamp() < throughputDeadlineTicks)
            {
                spotPub.Publish("bench", payload.AsSpan(), SendFlags.None);
                ReceiveSpotPayloadBlockingAndDrain(spotSub, recvPayload.AsSpan());
                recvCount++;
            }

            double elapsedSeconds = ElapsedSecondsFromTicks(benchStartTicks,
                Stopwatch.GetTimestamp());
            double thr = recvCount > 0 && elapsedSeconds > 0.0
                ? recvCount / elapsedSeconds
                : 0.0;

            if (drainMs > 0)
                Thread.Sleep(drainMs);

            var latSamples = new List<double>(latCount);
            long sampleSeen = 0;
            uint rng = 0xA341316Cu;
            for (int i = 0; i < latCount; i++)
            {
                long begin = Stopwatch.GetTimestamp();
                spotPub.Publish("bench", payload.AsSpan(), SendFlags.None);
                ReceiveSpotPayloadBlockingAndDrain(spotSub, recvPayload.AsSpan());
                long end = Stopwatch.GetTimestamp();
                double latUs = (end - begin) * 1_000_000.0 / Stopwatch.Frequency;
                ReservoirSample(latSamples, latUs, ref sampleSeen, latCount,
                    ref rng);
            }
            var latency = ComputeLatencyStats(latSamples);
            PrintResult("SPOT", transport, size, thr, latency.mean, latency.p95,
                latency.p99);
            wall.Stop();
            PrintSingleProcessMetrics("SPOT", transport, size, cpuStart, wall);
            return 0;
        }
        catch
        {
            return 2;
        }
        finally
        {
            TryDisposeAllQuietly(spotSub, spotPub, nodeSub, nodePub);
        }
    }

    private static void ReceiveSpotPayloadBlockingAndDrain(Spot spotSub,
        Span<byte> recvPayload)
    {
        _ = spotSub.ReceiveSinglePayload(recvPayload, ReceiveFlags.None);
        while (true)
        {
            try
            {
                if (spotSub.ReceiveSinglePayload(recvPayload,
                        ReceiveFlags.DontWait) <= 0)
                    break;
            }
            catch (ZlinkException ex) when (IsWouldBlock(ex.Errno)
                                            || IsInterrupted(ex.Errno))
            {
                break;
            }
        }
    }
}
