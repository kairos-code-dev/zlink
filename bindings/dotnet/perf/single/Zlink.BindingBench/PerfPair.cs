using System;
using System.Diagnostics;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    internal static int RunPair(string transport, int size)
    {
        int warmupSeconds = ParseEnv("PERF_SINGLE_WARMUP_SECONDS", 3);
        int settleMs = ParseEnv("PERF_SINGLE_SETTLE_MS", 300);
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnv("PERF_SINGLE_DRAIN_MS", 300);
        int latCount = ParseEnv("PERF_LAT_COUNT", 500);

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var left = new Zlink.Socket(ctx, SocketType.Pair);
        using var right = new Zlink.Socket(ctx, SocketType.Pair);
        ApplySingleSocketOptions(left);
        ApplySingleSocketOptions(right);

        try
        {
            string ep = EndpointFor(transport, "pair");
            left.Bind(ep);
            right.Connect(ep);
            Thread.Sleep(300);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            var recv = new byte[size];

            var warmupDeadline = DateTime.UtcNow.AddSeconds(Math.Max(0,
                warmupSeconds));
            while (DateTime.UtcNow < warmupDeadline)
            {
                SendRetry(right, buf.AsSpan(), SendFlags.None);
                ReceiveRetry(left, recv.AsSpan(), ReceiveFlags.None);
            }

            Thread.Sleep(Math.Max(0, settleMs));

            int recvCount = 0;
            var sw = Stopwatch.StartNew();
            var throughputDeadline = DateTime.UtcNow.AddSeconds(
                Math.Max(1, durationSeconds));
            while (DateTime.UtcNow < throughputDeadline)
            {
                SendRetry(right, buf.AsSpan(), SendFlags.None);
                int n = ReceiveRetry(left, recv.AsSpan(), ReceiveFlags.None);
                if (n == size)
                    recvCount++;
            }
            sw.Stop();
            double thr = recvCount > 0 && sw.Elapsed.TotalSeconds > 0.0
                ? recvCount / sw.Elapsed.TotalSeconds
                : 0.0;

            Thread.Sleep(Math.Max(0, drainMs));

            sw.Restart();
            for (int i = 0; i < latCount; i++)
            {
                SendRetry(right, buf.AsSpan(), SendFlags.None);
                int n = ReceiveRetry(left, recv.AsSpan(), ReceiveFlags.None);
                if (n != size)
                    return 2;
                SendRetry(left, recv.AsSpan(0, n), SendFlags.None);
                ReceiveRetry(right, recv.AsSpan(), ReceiveFlags.None);
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / (latCount * 2);

            PrintResult("PAIR", transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
    }

    internal static int RunDealerDealer(string transport, int size)
    {
        int warmupSeconds = ParseEnv("PERF_SINGLE_WARMUP_SECONDS", 3);
        int settleMs = ParseEnv("PERF_SINGLE_SETTLE_MS", 300);
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnv("PERF_SINGLE_DRAIN_MS", 300);
        int latCount = ParseEnv("PERF_LAT_COUNT", 500);

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var left = new Zlink.Socket(ctx, SocketType.Dealer);
        using var right = new Zlink.Socket(ctx, SocketType.Dealer);
        ApplySingleSocketOptions(left);
        ApplySingleSocketOptions(right);

        try
        {
            string ep = EndpointFor(transport, "dealer-dealer");
            left.Bind(ep);
            right.Connect(ep);
            Thread.Sleep(300);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            var recv = new byte[size];

            var warmupDeadline = DateTime.UtcNow.AddSeconds(Math.Max(0,
                warmupSeconds));
            while (DateTime.UtcNow < warmupDeadline)
            {
                SendRetry(right, buf.AsSpan(), SendFlags.None);
                ReceiveRetry(left, recv.AsSpan(), ReceiveFlags.None);
            }

            Thread.Sleep(Math.Max(0, settleMs));

            int recvCount = 0;
            var sw = Stopwatch.StartNew();
            var throughputDeadline = DateTime.UtcNow.AddSeconds(
                Math.Max(1, durationSeconds));
            while (DateTime.UtcNow < throughputDeadline)
            {
                SendRetry(right, buf.AsSpan(), SendFlags.None);
                int n = ReceiveRetry(left, recv.AsSpan(), ReceiveFlags.None);
                if (n == size)
                    recvCount++;
            }
            sw.Stop();
            double thr = recvCount > 0 && sw.Elapsed.TotalSeconds > 0.0
                ? recvCount / sw.Elapsed.TotalSeconds
                : 0.0;

            Thread.Sleep(Math.Max(0, drainMs));

            sw.Restart();
            for (int i = 0; i < latCount; i++)
            {
                SendRetry(right, buf.AsSpan(), SendFlags.None);
                int n = ReceiveRetry(left, recv.AsSpan(), ReceiveFlags.None);
                if (n != size)
                    return 2;
                SendRetry(left, recv.AsSpan(0, n), SendFlags.None);
                ReceiveRetry(right, recv.AsSpan(), ReceiveFlags.None);
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / (latCount * 2);

            PrintResult("DEALER_DEALER", transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
