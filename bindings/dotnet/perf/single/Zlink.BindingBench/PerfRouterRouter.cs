using System;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    internal static int RunRouterRouter(string transport, int size)
      => RunRouterRouterInternal(transport, size, false);

    internal static int RunRouterRouterPoll(string transport, int size)
      => RunRouterRouterInternal(transport, size, true);

    private static int RunRouterRouterInternal(string transport, int size, bool usePoll)
    {
        int warmupSeconds = ParseEnv("PERF_SINGLE_WARMUP_SECONDS", 3);
        int settleMs = ParseEnv("PERF_SINGLE_SETTLE_MS", 300);
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnv("PERF_SINGLE_DRAIN_MS", 300);
        int latCount = ParseEnv("PERF_LAT_COUNT", 1000);

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

            var warmupDeadline = DateTime.UtcNow.AddSeconds(Math.Max(0,
                warmupSeconds));
            while (DateTime.UtcNow < warmupDeadline)
            {
                SendRetry(router2, router1Id, SendFlags.SendMore);
                SendRetry(router2, buf.AsSpan(), SendFlags.None);
                if (usePoll && !WaitForInput(router1, 2000))
                    return 2;
                ReceiveRetry(router1, id.AsSpan(), ReceiveFlags.None);
                ReceiveRetry(router1, data.AsSpan(0, size), ReceiveFlags.None);
            }

            Thread.Sleep(Math.Max(0, settleMs));

            int recvCount = 0;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var throughputDeadline = DateTime.UtcNow.AddSeconds(
                Math.Max(1, durationSeconds));
            while (DateTime.UtcNow < throughputDeadline)
            {
                SendRetry(router2, router1Id, SendFlags.SendMore);
                SendRetry(router2, buf.AsSpan(), SendFlags.None);
                if (usePoll && !WaitForInput(router1, 2000))
                    return 2;
                ReceiveRetry(router1, id.AsSpan(), ReceiveFlags.None);
                int n = ReceiveRetry(router1, data.AsSpan(0, size), ReceiveFlags.None);
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
                SendRetry(router2, router1Id, SendFlags.SendMore);
                SendRetry(router2, buf.AsSpan(), SendFlags.None);

                if (usePoll && !WaitForInput(router1, 2000))
                    return 2;
                int idLen = ReceiveRetry(router1, id.AsSpan(), ReceiveFlags.None);
                ReceiveRetry(router1, data.AsSpan(0, size), ReceiveFlags.None);

                SendRetry(router1, id.AsSpan(0, idLen), SendFlags.SendMore);
                SendRetry(router1, buf.AsSpan(), SendFlags.None);

                if (usePoll && !WaitForInput(router2, 2000))
                    return 2;
                ReceiveRetry(router2, id.AsSpan(), ReceiveFlags.None);
                ReceiveRetry(router2, data.AsSpan(0, size), ReceiveFlags.None);
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / (latCount * 2);
            PrintResult(usePoll ? "ROUTER_ROUTER_POLL" : "ROUTER_ROUTER", transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
    }
}
