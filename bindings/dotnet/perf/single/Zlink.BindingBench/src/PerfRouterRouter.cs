using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using Zlink;
using static PerfRunner;

internal static class PerfRouterRouter
{
    internal static int RunRouterRouter(string transport, int size)
      => RunRouterRouterInternal(transport, size, false);

    internal static int RunRouterRouterInternal(string transport, int size,
        bool usePoll)
    {
        const int connectRetryCount = 100;
        const int connectRetrySleepMs = 10;
        string pattern = usePoll ? "ROUTER_ROUTER_POLL" : "ROUTER_ROUTER";
        int warmupCount = ResolveSingleWarmupCount(pattern);
        int settleMs = SingleSettleTimeMs;
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnvNonNegative("PERF_SINGLE_RCVTIMEO_MS", 200);
        int latCount = ResolveSingleLatencyCount(pattern);

        var process = Process.GetCurrentProcess();
        TimeSpan cpuStart = process.TotalProcessorTime;
        var wall = Stopwatch.StartNew();
        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var router1 = new Zlink.Socket(ctx, SocketType.Router);
        using var router2 = new Zlink.Socket(ctx, SocketType.Router);
        ApplySingleSocketOptions(router1);
        ApplySingleSocketOptions(router2);
        ConfigureTlsServerIfNeeded(router1, transport);
        ConfigureTlsClientIfNeeded(router2, transport);

        try
        {
            string ep = EndpointFor(transport, "router-router");
            string router1Name = "ROUTER1";
            string router2Name = "ROUTER2";
            ReadOnlySpan<byte> router1Id = "ROUTER1"u8;
            ReadOnlySpan<byte> router2Id = "ROUTER2"u8;
            ReadOnlySpan<byte> ping = "PING"u8;
            ReadOnlySpan<byte> pong = "PONG"u8;
            router1.SetOption(SocketOptions.RoutingId, router1Name);
            router2.SetOption(SocketOptions.RoutingId, router2Name);
            router1.SetOption(SocketOptions.RouterMandatory, 1);
            router2.SetOption(SocketOptions.RouterMandatory, 1);
            router1.Bind(ep);
            router2.Connect(ep);
            Thread.Sleep(SingleConnectWaitMs);

            var id = new byte[256];
            var data = new byte[Math.Max(256, size)];
            bool connected = false;
            for (int i = 0; i < connectRetryCount; i++)
            {
                try
                {
                    router2.Send(router1Id, SendFlags.SendMore | SendFlags.DontWait);
                    router2.Send(ping, SendFlags.DontWait);
                }
                catch
                {
                    Thread.Sleep(connectRetrySleepMs);
                    continue;
                }

                try
                {
                    router1.Receive(id.AsSpan(), ReceiveFlags.DontWait);
                    router1.Receive(data.AsSpan(), ReceiveFlags.DontWait);
                    DrainRemainingFramesNonBlocking(router1);
                    connected = true;
                    break;
                }
                catch
                {
                    Thread.Sleep(connectRetrySleepMs);
                }
            }

            if (!connected)
                return 2;

            router1.Send(router2Id, SendFlags.SendMore);
            router1.Send(pong, SendFlags.None);
            router2.Receive(id.AsSpan(), ReceiveFlags.None);
            router2.Receive(data.AsSpan(), ReceiveFlags.None);
            DrainRemainingFramesNonBlocking(router2);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');

            for (int i = 0; i < warmupCount; i++)
            {
                SendBlocking(router2, router1Id, SendFlags.SendMore);
                SendBlocking(router2, buf.AsSpan(), SendFlags.None);
                ReceiveBlocking(router1, id.AsSpan(), ReceiveFlags.None);
                ReceiveBlocking(router1, data.AsSpan(0, size), ReceiveFlags.None);
                DrainRemainingFramesNonBlocking(router1);
            }

            Thread.Sleep(settleMs);

            int recvCount = 0;
            long benchStartTicks = Stopwatch.GetTimestamp();
            long throughputDeadlineTicks = DeadlineTicksFromSeconds(
                durationSeconds);
            while (Stopwatch.GetTimestamp() < throughputDeadlineTicks)
            {
                SendBlocking(router2, router1Id, SendFlags.SendMore);
                SendBlocking(router2, buf.AsSpan(), SendFlags.None);
                ReceiveBlocking(router1, id.AsSpan(), ReceiveFlags.None);
                int n = ReceiveBlocking(router1, data.AsSpan(0, size), ReceiveFlags.None);
                DrainRemainingFramesNonBlocking(router1);
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
                SendBlocking(router2, router1Id, SendFlags.SendMore);
                SendBlocking(router2, buf.AsSpan(), SendFlags.None);
                int idLen = ReceiveBlocking(router1, id.AsSpan(), ReceiveFlags.None);
                ReceiveBlocking(router1, data.AsSpan(0, size), ReceiveFlags.None);
                DrainRemainingFramesNonBlocking(router1);

                SendBlocking(router1, id.AsSpan(0, idLen), SendFlags.SendMore);
                SendBlocking(router1, buf.AsSpan(), SendFlags.None);
                ReceiveBlocking(router2, id.AsSpan(), ReceiveFlags.None);
                ReceiveBlocking(router2, data.AsSpan(0, size), ReceiveFlags.None);
                DrainRemainingFramesNonBlocking(router2);
                long end = Stopwatch.GetTimestamp();
                double oneWayUs = ((end - begin) * 1_000_000.0
                                   / Stopwatch.Frequency) / 2.0;
                ReservoirSample(latSamples, oneWayUs, ref sampleSeen, latCount,
                    ref rng);
            }

            var latency = ComputeLatencyStats(latSamples);
            PrintResult(pattern, transport, size, thr, latency.mean, latency.p95,
                latency.p99);
            wall.Stop();
            PrintSingleProcessMetrics(pattern, transport, size, cpuStart, wall);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
