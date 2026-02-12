using System;
using System.Threading;
using Zlink;

internal static partial class BenchRunner
{
    internal static int RunPubSub(string transport, int size)
    {
        int warmup = ParseEnv("BENCH_WARMUP_COUNT", 1000);
        int msgCount = ResolveMsgCount(size);

        using var ctx = new Context();
        using var pub = new Zlink.Socket(ctx, SocketType.Pub);
        using var sub = new Zlink.Socket(ctx, SocketType.Sub);

        try
        {
            string ep = EndpointFor(transport, "pubsub");
            sub.SetOption(SocketOption.Subscribe, Array.Empty<byte>());
            pub.Bind(ep);
            sub.Connect(ep);
            Thread.Sleep(300);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');
            var recv = new byte[Math.Max(256, size)];

            for (int i = 0; i < warmup; i++)
            {
                pub.Send(buf, SendFlags.None);
                sub.Receive(recv.AsSpan(0, size).ToArray(), ReceiveFlags.None);
            }

            var recvDone = new ManualResetEventSlim(false);
            Exception? recvError = null;
            var th = new Thread(() =>
            {
                try
                {
                    for (int i = 0; i < msgCount; i++)
                        sub.Receive(recv.AsSpan(0, size).ToArray(), ReceiveFlags.None);
                    recvDone.Set();
                }
                catch (Exception ex)
                {
                    recvError = ex;
                }
            });

            th.Start();
            var sw = System.Diagnostics.Stopwatch.StartNew();
            for (int i = 0; i < msgCount; i++)
                pub.Send(buf, SendFlags.None);
            th.Join();
            sw.Stop();

            if (recvError != null || !recvDone.IsSet)
                return 2;

            double thr = msgCount / sw.Elapsed.TotalSeconds;
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / msgCount;
            PrintResult("PUBSUB", transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
