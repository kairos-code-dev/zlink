using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfDealerRouter
{
    internal static int RunDealerRouter(string transport, int size)
    {
        int warmupCount = ResolveSingleWarmupCount("DEALER_ROUTER");
        int settleMs = SingleSettleTimeMs;
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount("DEALER_ROUTER");

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();
        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var router = new Zlink.Socket(ctx, SocketType.Router);
        using var dealer = new Zlink.Socket(ctx, SocketType.Dealer);
        ApplySingleSocketOptions(router);
        ApplySingleSocketOptions(dealer);
        ConfigureTlsServerIfNeeded(router, transport);
        ConfigureTlsClientIfNeeded(dealer, transport);

        try
        {
            string ep = EndpointFor(transport, "dealer-router");
            dealer.SetOption(SocketOptions.RoutingId, "CLIENT");
            router.Bind(ep);
            dealer.Connect(ep);
            Thread.Sleep(SingleConnectWaitMs);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            var rid = new byte[256];
            var recv = new byte[Math.Max(256, size)];

            for (int i = 0; i < warmupCount; i++)
            {
                SendBlocking(dealer, buf.AsSpan(), SendFlags.None);
                ReceiveBlocking(router, rid.AsSpan(), ReceiveFlags.None);
                ReceiveBlocking(router, recv.AsSpan(0, size), ReceiveFlags.None);
                DrainRemainingFramesNonBlocking(router);
            }

            Thread.Sleep(settleMs);

            int recvCount = 0;
            long benchStartTicks = Stopwatch.GetTimestamp();
            long throughputDeadlineTicks = DeadlineTicksFromSeconds(
                durationSeconds);
            while (Stopwatch.GetTimestamp() < throughputDeadlineTicks)
            {
                SendBlocking(dealer, buf.AsSpan(), SendFlags.None);
                ReceiveBlocking(router, rid.AsSpan(), ReceiveFlags.None);
                int n = ReceiveBlocking(router, recv.AsSpan(0, size), ReceiveFlags.None);
                DrainRemainingFramesNonBlocking(router);
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
                SendBlocking(dealer, buf.AsSpan(), SendFlags.None);
                int ridLen = ReceiveBlocking(router, rid.AsSpan(), ReceiveFlags.None);
                ReceiveBlocking(router, recv.AsSpan(0, size), ReceiveFlags.None);
                DrainRemainingFramesNonBlocking(router);
                SendBlocking(router, rid.AsSpan(0, ridLen), SendFlags.SendMore);
                SendBlocking(router, buf.AsSpan(), SendFlags.None);
                ReceiveBlocking(dealer, recv.AsSpan(0, size), ReceiveFlags.None);
                long end = Stopwatch.GetTimestamp();
                double oneWayUs = ((end - begin) * 1_000_000.0
                                   / Stopwatch.Frequency) / 2.0;
                ReservoirSample(latSamples, oneWayUs, ref sampleSeen, latCount,
                    ref rng);
            }
            var latency = ComputeLatencyStats(latSamples);
            PrintResult("DEALER_ROUTER", transport, size, thr, latency.mean,
                latency.p95, latency.p99);
            wall.Stop();
            PrintSingleProcessMetrics("DEALER_ROUTER", transport, size, cpuStart,
                wall);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
