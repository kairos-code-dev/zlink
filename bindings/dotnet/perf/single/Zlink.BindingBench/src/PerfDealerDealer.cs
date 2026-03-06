using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfDealerDealer
{
    internal static int RunDealerDealer(string transport, int size)
    {
        int warmupCount = ResolveSingleWarmupCount("DEALER_DEALER");
        int settleMs = SingleSettleTimeMs;
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount("DEALER_DEALER");

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();
        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var left = new Zlink.Socket(ctx, SocketType.Dealer);
        using var right = new Zlink.Socket(ctx, SocketType.Dealer);
        ApplySingleSocketOptions(left);
        ApplySingleSocketOptions(right);
        ConfigureTlsServerIfNeeded(left, transport);
        ConfigureTlsClientIfNeeded(right, transport);

        try
        {
            string ep = EndpointFor(transport, "dealer-dealer");
            left.Bind(ep);
            right.Connect(ep);
            Thread.Sleep(SingleConnectWaitMs);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            var recv = new byte[size];

            for (int i = 0; i < warmupCount; i++)
            {
                SendBlocking(right, buf.AsSpan(), SendFlags.None);
                ReceiveBlocking(left, recv.AsSpan(), ReceiveFlags.None);
            }

            Thread.Sleep(settleMs);

            int recvCount = 0;
            long benchStartTicks = Stopwatch.GetTimestamp();
            long throughputDeadlineTicks = DeadlineTicksFromSeconds(
                durationSeconds);
            while (Stopwatch.GetTimestamp() < throughputDeadlineTicks)
            {
                SendBlocking(right, buf.AsSpan(), SendFlags.None);
                int n = ReceiveBlocking(left, recv.AsSpan(), ReceiveFlags.None);
                if (n == size)
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
                SendBlocking(right, buf.AsSpan(), SendFlags.None);
                int n = ReceiveBlocking(left, recv.AsSpan(), ReceiveFlags.None);
                if (n != size)
                    return 2;

                SendBlocking(left, recv.AsSpan(0, n), SendFlags.None);
                ReceiveBlocking(right, recv.AsSpan(), ReceiveFlags.None);
                long end = Stopwatch.GetTimestamp();
                double oneWayUs = ((end - begin) * 1_000_000.0
                                   / Stopwatch.Frequency) / 2.0;
                ReservoirSample(latSamples, oneWayUs, ref sampleSeen, latCount,
                    ref rng);
            }

            var latency = ComputeLatencyStats(latSamples);
            PrintResult("DEALER_DEALER", transport, size, thr, latency.mean,
                latency.p95, latency.p99);
            wall.Stop();
            PrintSingleProcessMetrics("DEALER_DEALER", transport, size, cpuStart,
                wall);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
