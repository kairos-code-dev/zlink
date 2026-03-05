using System;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    internal static int RunDealerRouter(string transport, int size)
    {
        int warmupSeconds = ParseEnv("PERF_SINGLE_WARMUP_SECONDS", 3);
        int settleMs = ParseEnv("PERF_SINGLE_SETTLE_MS", 300);
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnv("PERF_SINGLE_DRAIN_MS", 300);
        int latCount = ParseEnv("PERF_LAT_COUNT", 1000);

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
        using var router = new Zlink.Socket(ctx, SocketType.Router);
        using var dealer = new Zlink.Socket(ctx, SocketType.Dealer);
        ApplySingleSocketOptions(router);
        ApplySingleSocketOptions(dealer);

        try
        {
            string ep = EndpointFor(transport, "dealer-router");
            dealer.SetOption(SocketOptions.RoutingId, "CLIENT");
            router.Bind(ep);
            dealer.Connect(ep);
            Thread.Sleep(300);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            var rid = new byte[256];
            var recv = new byte[Math.Max(256, size)];

            var warmupDeadline = DateTime.UtcNow.AddSeconds(Math.Max(0,
                warmupSeconds));
            while (DateTime.UtcNow < warmupDeadline)
            {
                SendRetry(dealer, buf.AsSpan(), SendFlags.None);
                ReceiveRetry(router, rid.AsSpan(), ReceiveFlags.None);
                ReceiveRetry(router, recv.AsSpan(0, size), ReceiveFlags.None);
            }

            Thread.Sleep(Math.Max(0, settleMs));

            int recvCount = 0;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var throughputDeadline = DateTime.UtcNow.AddSeconds(
                Math.Max(1, durationSeconds));
            while (DateTime.UtcNow < throughputDeadline)
            {
                SendRetry(dealer, buf.AsSpan(), SendFlags.None);
                ReceiveRetry(router, rid.AsSpan(), ReceiveFlags.None);
                int n = ReceiveRetry(router, recv.AsSpan(0, size), ReceiveFlags.None);
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
                SendRetry(dealer, buf.AsSpan(), SendFlags.None);
                int ridLen = ReceiveRetry(router, rid.AsSpan(), ReceiveFlags.None);
                ReceiveRetry(router, recv.AsSpan(0, size), ReceiveFlags.None);
                SendRetry(router, rid.AsSpan(0, ridLen), SendFlags.SendMore);
                SendRetry(router, buf.AsSpan(), SendFlags.None);
                ReceiveRetry(dealer, recv.AsSpan(0, size), ReceiveFlags.None);
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / (latCount * 2);
            PrintResult("DEALER_ROUTER", transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
