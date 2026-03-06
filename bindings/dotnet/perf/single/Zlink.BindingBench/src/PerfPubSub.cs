using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfPubSub
{
    internal static int RunPubSub(string transport, int size)
    {
        int warmupCount = ResolveSingleWarmupCount("PUBSUB");
        int settleMs = SingleSettleTimeMs;
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount("PUBSUB");

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();
        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var pub = new Zlink.Socket(ctx, SocketType.Pub);
        using var sub = new Zlink.Socket(ctx, SocketType.Sub);
        ApplySingleSocketOptions(pub);
        ApplySingleSocketOptions(sub);
        ConfigureTlsServerIfNeeded(pub, transport);
        ConfigureTlsClientIfNeeded(sub, transport);

        try
        {
            string ep = EndpointFor(transport, "pubsub");
            sub.SetOption(SocketOptions.Subscribe, string.Empty);
            pub.Bind(ep);
            sub.Connect(ep);
            Thread.Sleep(SingleConnectWaitMs);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            var recv = new byte[Math.Max(256, size)];
            var subPoller = new Poller();
            subPoller.Add(sub, PollEvents.PollIn);
            var subEvents = new PollEvent[1];

            bool ready = false;
            for (int i = 0; i < 2000; i++)
            {
                SendBlocking(pub, buf.AsSpan(), SendFlags.None);
                if (!WaitForInput(subPoller, subEvents, 1))
                    continue;
                int n = ReceiveBlocking(sub, recv.AsSpan(0, size), ReceiveFlags.None);
                if (n == size)
                {
                    ready = true;
                    break;
                }
            }
            if (!ready)
                return 2;

            int warmed = 0;
            while (warmed < warmupCount)
            {
                SendBlocking(pub, buf.AsSpan(), SendFlags.None);
                if (!WaitForInput(subPoller, subEvents, 10))
                    continue;
                int n = ReceiveBlocking(sub, recv.AsSpan(0, size), ReceiveFlags.None);
                if (n == size)
                    warmed++;
            }

            Thread.Sleep(settleMs);

            int recvCount = 0;
            long benchStartTicks = Stopwatch.GetTimestamp();
            long throughputDeadlineTicks = DeadlineTicksFromSeconds(
                durationSeconds);
            while (Stopwatch.GetTimestamp() < throughputDeadlineTicks)
            {
                SendBlocking(pub, buf.AsSpan(), SendFlags.None);
                if (!WaitForInput(subPoller, subEvents, 10))
                    continue;
                int n = ReceiveBlocking(sub, recv.AsSpan(0, size), ReceiveFlags.None);
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
                bool measured = false;
                for (int attempt = 0; attempt < 1024; attempt++)
                {
                    long begin = Stopwatch.GetTimestamp();
                    SendBlocking(pub, buf.AsSpan(), SendFlags.None);
                    if (!WaitForInput(subPoller, subEvents, 10))
                        continue;

                    int n = ReceiveBlocking(sub, recv.AsSpan(0, size),
                        ReceiveFlags.None);
                    if (n != size)
                        continue;

                    long end = Stopwatch.GetTimestamp();
                    double latUs = (end - begin) * 1_000_000.0
                        / Stopwatch.Frequency;
                    ReservoirSample(latSamples, latUs, ref sampleSeen, latCount,
                        ref rng);
                    measured = true;
                    break;
                }
                if (!measured)
                    return 2;
            }
            var latency = ComputeLatencyStats(latSamples);

            PrintResult("PUBSUB", transport, size, thr, latency.mean,
                latency.p95, latency.p99);
            wall.Stop();
            PrintSingleProcessMetrics("PUBSUB", transport, size, cpuStart, wall);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
