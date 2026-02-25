using System;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    internal static int RunPubSub(string transport, int size)
    {
        int warmupSeconds = ParseEnv("PERF_SINGLE_WARMUP_SECONDS", 3);
        int settleMs = ParseEnv("PERF_SINGLE_SETTLE_MS", 300);
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnv("PERF_SINGLE_DRAIN_MS", 300);
        int latCount = ParseEnv("PERF_LAT_COUNT", 500);

        using var ctx = new Context();
        using var pub = new Zlink.Socket(ctx, SocketType.Pub);
        using var sub = new Zlink.Socket(ctx, SocketType.Sub);

        try
        {
            string ep = EndpointFor(transport, "pubsub");
            sub.SetOption(SocketOption.Subscribe, ReadOnlySpan<byte>.Empty);
            pub.Bind(ep);
            sub.Connect(ep);
            Thread.Sleep(300);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            var recv = new byte[Math.Max(256, size)];

            var warmupDeadline = DateTime.UtcNow.AddSeconds(Math.Max(0,
                warmupSeconds));
            while (DateTime.UtcNow < warmupDeadline)
            {
                SendRetry(pub, buf.AsSpan(), SendFlags.None);
                ReceiveRetry(sub, recv.AsSpan(0, size), ReceiveFlags.None);
            }

            Thread.Sleep(Math.Max(0, settleMs));

            int recvCount = 0;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var throughputDeadline = DateTime.UtcNow.AddSeconds(
                Math.Max(1, durationSeconds));
            while (DateTime.UtcNow < throughputDeadline)
            {
                SendRetry(pub, buf.AsSpan(), SendFlags.None);
                int n = ReceiveRetry(sub, recv.AsSpan(0, size), ReceiveFlags.None);
                if (n == size)
                    recvCount++;
            }
            sw.Stop();
            double thr = recvCount > 0 && sw.Elapsed.TotalSeconds > 0.0
                ? recvCount / sw.Elapsed.TotalSeconds
                : 0.0;

            Thread.Sleep(Math.Max(0, drainMs));

            int received = 0;
            sw.Restart();
            for (int i = 0; i < latCount; i++)
            {
                SendRetry(pub, buf.AsSpan(), SendFlags.None);
                int n = ReceiveRetry(sub, recv.AsSpan(0, size), ReceiveFlags.None);
                if (n == size)
                    received++;
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0)
                / Math.Max(1, received);
            PrintResult("PUBSUB", transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
