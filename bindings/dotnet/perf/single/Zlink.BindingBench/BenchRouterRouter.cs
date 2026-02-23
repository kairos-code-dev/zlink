using System;
using System.Threading;
using Zlink;

internal static partial class BenchRunner
{
    internal static int RunRouterRouter(string transport, int size)
      => RunRouterRouterInternal(transport, size, false);

    internal static int RunRouterRouterPoll(string transport, int size)
      => RunRouterRouterInternal(transport, size, true);

    private static int RunRouterRouterInternal(string transport, int size, bool usePoll)
    {
        int latCount = ParseEnv("BENCH_LAT_COUNT", 1000);
        int msgCount = ResolveMsgCount(size);

        using var ctx = new Context();
        using var router1 = new Zlink.Socket(ctx, SocketType.Router);
        using var router2 = new Zlink.Socket(ctx, SocketType.Router);

        try
        {
            string ep = EndpointFor(transport, "router-router");
            ReadOnlySpan<byte> router1Id = "ROUTER1"u8;
            ReadOnlySpan<byte> router2Id = "ROUTER2"u8;
            ReadOnlySpan<byte> ping = "PING"u8;
            ReadOnlySpan<byte> pong = "PONG"u8;
            router1.SetOption(SocketOption.RoutingId, router1Id);
            router2.SetOption(SocketOption.RoutingId, router2Id);
            router1.SetOption(SocketOption.RouterMandatory, 1);
            router2.SetOption(SocketOption.RouterMandatory, 1);
            router1.Bind(ep);
            router2.Connect(ep);
            Thread.Sleep(300);

            var id = new byte[256];
            var data = new byte[Math.Max(256, size)];
            bool connected = false;
            for (int i = 0; i < 100; i++)
            {
                try
                {
                    router2.Send(router1Id, SendFlags.SendMore | SendFlags.DontWait);
                    router2.Send(ping, SendFlags.DontWait);
                }
                catch
                {
                    Thread.Sleep(10);
                    continue;
                }

                if (usePoll && !WaitForInput(router1, 0))
                {
                    Thread.Sleep(10);
                    continue;
                }

                try
                {
                    router1.Receive(id.AsSpan(), ReceiveFlags.DontWait);
                    router1.Receive(data.AsSpan(), ReceiveFlags.DontWait);
                    connected = true;
                    break;
                }
                catch
                {
                    Thread.Sleep(10);
                }
            }

            if (!connected)
                return 2;

            router1.Send(router2Id, SendFlags.SendMore);
            router1.Send(pong, SendFlags.None);

            if (usePoll && !WaitForInput(router2, 2000))
                return 2;
            router2.Receive(id.AsSpan(), ReceiveFlags.None);
            router2.Receive(data.AsSpan(), ReceiveFlags.None);

            var buf = new byte[size];
            Array.Fill(buf, (byte)'a');

            var sw = System.Diagnostics.Stopwatch.StartNew();
            for (int i = 0; i < latCount; i++)
            {
                router2.Send(router1Id, SendFlags.SendMore);
                router2.Send(buf.AsSpan(), SendFlags.None);

                if (usePoll && !WaitForInput(router1, 2000))
                    return 2;
                int idLen = router1.Receive(id.AsSpan(), ReceiveFlags.None);
                router1.Receive(data.AsSpan(0, size), ReceiveFlags.None);

                router1.Send(id.AsSpan(0, idLen), SendFlags.SendMore);
                router1.Send(buf.AsSpan(), SendFlags.None);

                if (usePoll && !WaitForInput(router2, 2000))
                    return 2;
                router2.Receive(id.AsSpan(), ReceiveFlags.None);
                router2.Receive(data.AsSpan(0, size), ReceiveFlags.None);
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / (latCount * 2);

            var recvDone = new ManualResetEventSlim(false);
            Exception? recvError = null;
            var receiver = new Thread(() =>
            {
                try
                {
                    for (int i = 0; i < msgCount; i++)
                    {
                        if (usePoll && !WaitForInput(router1, 2000))
                            throw new TimeoutException("poll timeout");
                        router1.Receive(id.AsSpan(), ReceiveFlags.None);
                        router1.Receive(data.AsSpan(0, size), ReceiveFlags.None);
                    }
                    recvDone.Set();
                }
                catch (Exception ex)
                {
                    recvError = ex;
                }
            });

            receiver.Start();
            sw.Restart();
            for (int i = 0; i < msgCount; i++)
            {
                router2.Send(router1Id, SendFlags.SendMore);
                router2.Send(buf.AsSpan(), SendFlags.None);
            }
            receiver.Join();
            sw.Stop();

            if (recvError != null || !recvDone.IsSet)
                return 2;

            double thr = msgCount / sw.Elapsed.TotalSeconds;
            PrintResult(usePoll ? "ROUTER_ROUTER_POLL" : "ROUTER_ROUTER", transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
