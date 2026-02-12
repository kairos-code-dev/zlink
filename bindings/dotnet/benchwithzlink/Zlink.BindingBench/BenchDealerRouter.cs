using System;
using System.Text;
using System.Threading;
using Zlink;

internal static partial class BenchRunner
{
    internal static int RunDealerRouter(string transport, int size)
    {
        int warmup = ParseEnv("BENCH_WARMUP_COUNT", 1000);
        int latCount = ParseEnv("BENCH_LAT_COUNT", 1000);
        int msgCount = ResolveMsgCount(size);

        using var ctx = new Context();
        using var router = new Zlink.Socket(ctx, SocketType.Router);
        using var dealer = new Zlink.Socket(ctx, SocketType.Dealer);

        try
        {
            string ep = EndpointFor(transport, "dealer-router");
            dealer.SetOption(SocketOption.RoutingId, Encoding.UTF8.GetBytes("CLIENT"));
            router.Bind(ep);
            dealer.Connect(ep);
            Thread.Sleep(300);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            var rid = new byte[256];
            var recv = new byte[Math.Max(256, size)];

            for (int i = 0; i < warmup; i++)
            {
                dealer.Send(buf, SendFlags.None);
                int ridLen = router.Receive(rid, ReceiveFlags.None);
                router.Receive(recv.AsSpan(0, size).ToArray(), ReceiveFlags.None);
                router.Send(rid.AsSpan(0, ridLen).ToArray(), SendFlags.SendMore);
                router.Send(buf, SendFlags.None);
                dealer.Receive(recv.AsSpan(0, size).ToArray(), ReceiveFlags.None);
            }

            var sw = System.Diagnostics.Stopwatch.StartNew();
            for (int i = 0; i < latCount; i++)
            {
                dealer.Send(buf, SendFlags.None);
                int ridLen = router.Receive(rid, ReceiveFlags.None);
                router.Receive(recv.AsSpan(0, size).ToArray(), ReceiveFlags.None);
                router.Send(rid.AsSpan(0, ridLen).ToArray(), SendFlags.SendMore);
                router.Send(buf, SendFlags.None);
                dealer.Receive(recv.AsSpan(0, size).ToArray(), ReceiveFlags.None);
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / (latCount * 2);

            var recvDone = new ManualResetEventSlim(false);
            Exception? recvError = null;
            var th = new Thread(() =>
            {
                try
                {
                    for (int i = 0; i < msgCount; i++)
                    {
                        router.Receive(rid, ReceiveFlags.None);
                        router.Receive(recv.AsSpan(0, size).ToArray(), ReceiveFlags.None);
                    }
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
                dealer.Send(buf, SendFlags.None);
            th.Join();
            sw.Stop();

            if (recvError != null || !recvDone.IsSet)
                return 2;

            double thr = msgCount / sw.Elapsed.TotalSeconds;
            PrintResult("DEALER_ROUTER", transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
