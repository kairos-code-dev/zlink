using System;
using System.Diagnostics;
using System.Threading;
using Zlink;

internal static partial class BenchRunner
{
    internal static int RunPair(string transport, int size) =>
        RunPairLike("PAIR", SocketType.Pair, SocketType.Pair, transport, size);

    internal static int RunDealerDealer(string transport, int size) =>
        RunPairLike("DEALER_DEALER", SocketType.Dealer, SocketType.Dealer, transport, size);

    private static int RunPairLike(string outPattern, SocketType aType, SocketType bType, string transport, int size)
    {
        int warmup = ParseEnv("BENCH_WARMUP_COUNT", 1000);
        int latCount = ParseEnv("BENCH_LAT_COUNT", 500);
        int msgCount = ResolveMsgCount(size);

        using var ctx = new Context();
        using var a = new Zlink.Socket(ctx, aType);
        using var b = new Zlink.Socket(ctx, bType);

        try
        {
            string ep = EndpointFor(transport, outPattern.ToLowerInvariant());
            a.Bind(ep);
            b.Connect(ep);
            Thread.Sleep(300);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            var recv = new byte[size];

            for (int i = 0; i < warmup; i++)
            {
                SendRetry(b, buf.AsSpan(), SendFlags.None);
                ReceiveRetry(a, recv.AsSpan(), ReceiveFlags.None);
            }

            var sw = Stopwatch.StartNew();
            for (int i = 0; i < latCount; i++)
            {
                SendRetry(b, buf.AsSpan(), SendFlags.None);
                int n = ReceiveRetry(a, recv.AsSpan(), ReceiveFlags.None);
                if (n != size)
                    return 2;
                SendRetry(a, recv.AsSpan(0, n), SendFlags.None);
                ReceiveRetry(b, recv.AsSpan(), ReceiveFlags.None);
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / (latCount * 2);

            var recvDone = new ManualResetEventSlim(false);
            Exception? recvError = null;
            var th = new Thread(() =>
            {
                var recvThr = new byte[size];
                try
                {
                    for (int i = 0; i < msgCount; i++)
                        ReceiveRetry(a, recvThr.AsSpan(), ReceiveFlags.None);
                    recvDone.Set();
                }
                catch (Exception ex)
                {
                    recvError = ex;
                }
            });

            th.Start();
            sw.Restart();
            for (int i = 0; i < msgCount; i++)
                SendRetry(b, buf.AsSpan(), SendFlags.None);
            th.Join();
            sw.Stop();

            if (recvError != null || !recvDone.IsSet)
                return 2;

            double thr = msgCount / sw.Elapsed.TotalSeconds;
            PrintResult(outPattern, transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
