using System;
using System.Threading;
using Zlink;

internal static partial class BenchRunner
{
    internal static int RunGateway(string transport, int size)
    {
        int warmup = ParseEnv("BENCH_WARMUP_COUNT", 200);
        int latCount = ParseEnv("BENCH_LAT_COUNT", 200);
        int msgCount = ResolveMsgCount(size);

        using var ctx = new Context();
        Registry? registry = null;
        Discovery? discovery = null;
        Receiver? receiver = null;
        Zlink.Socket? router = null;
        Gateway? gateway = null;

        try
        {
            string suffix = $"{DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()}-{Guid.NewGuid()}";
            string regPub = $"inproc://gw-pub-{suffix}";
            string regRouter = $"inproc://gw-router-{suffix}";

            registry = new Registry(ctx);
            registry.SetHeartbeat(5000, 60000);
            registry.SetEndpoints(regPub, regRouter);
            registry.Start();

            discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
            discovery.ConnectRegistry(regPub);
            string service = "svc";
            discovery.Subscribe(service);

            receiver = new Receiver(ctx);
            string providerEp = EndpointFor(transport, "gateway-provider");
            receiver.Bind(providerEp);
            receiver.ConnectRegistry(regRouter);
            receiver.Register(service, providerEp, 1);
            router = receiver.CreateRouterSocket();
            router.SetOption(SocketOption.RcvTimeo, 5000);

            gateway = new Gateway(ctx, discovery);
            if (!WaitUntil(() => discovery.ReceiverCount(service) > 0, 5000))
                return 2;
            if (!WaitUntil(() => gateway.ConnectionCount(service) > 0, 5000))
                return 2;
            byte[] targetRoutingId = Array.Empty<byte>();
            if (!WaitUntil(() =>
                {
                    try
                    {
                        var receivers = discovery.GetReceivers(service);
                        if (receivers.Length == 0)
                            return false;
                        if (receivers[0].RoutingId.Length == 0)
                            return false;
                        targetRoutingId = receivers[0].RoutingId;
                        return true;
                    }
                    catch
                    {
                        return false;
                    }
                }, 5000))
                return 2;
            Thread.Sleep(300);

            var payload = new byte[size];
            Array.Fill(payload, (byte)'a');
            var rid = new byte[256];
            var data = new byte[Math.Max(256, size)];

            for (int i = 0; i < warmup; i++)
            {
                gateway.Send(service, payload.AsSpan(), SendFlags.None);
                GatewayReceiveProviderMessage(router, rid.AsSpan(),
                    data.AsSpan());
            }

            var sw = System.Diagnostics.Stopwatch.StartNew();
            for (int i = 0; i < latCount; i++)
            {
                gateway.Send(service, payload.AsSpan(), SendFlags.None);
                GatewayReceiveProviderMessage(router, rid.AsSpan(),
                    data.AsSpan());
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0) / latCount;

            int recvCount = 0;
            var recvThread = new Thread(() =>
            {
                for (int i = 0; i < msgCount; i++)
                {
                    try
                    {
                        GatewayReceiveProviderMessage(router, rid.AsSpan(),
                            data.AsSpan());
                        recvCount++;
                    }
                    catch
                    {
                        break;
                    }
                }
            });

            recvThread.Start();
            int sent = 0;
            sw.Restart();
            for (int i = 0; i < msgCount; i++)
            {
                try
                {
                    gateway.Send(service, payload.AsSpan(), SendFlags.None);
                }
                catch
                {
                    break;
                }
                sent++;
            }
            recvThread.Join();
            sw.Stop();

            double thr = (sent > 0 && recvCount > 0)
              ? Math.Min(sent, recvCount) / sw.Elapsed.TotalSeconds
              : 0.0;
            PrintResult("GATEWAY", transport, size, thr, latUs);
            return 0;
        }
        catch
        {
            return 2;
        }
        finally
        {
            try { gateway?.Dispose(); } catch { }
            try { router?.Dispose(); } catch { }
            try { receiver?.Dispose(); } catch { }
            try { discovery?.Dispose(); } catch { }
            try { registry?.Dispose(); } catch { }
        }
    }
}
