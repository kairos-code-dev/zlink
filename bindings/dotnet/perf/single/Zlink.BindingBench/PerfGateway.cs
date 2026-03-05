using System;
using System.Threading;
using Zlink;

internal static partial class PerfRunner
{
    internal static int RunGateway(string transport, int size)
    {
        int warmupSeconds = ParseEnv("PERF_SINGLE_WARMUP_SECONDS", 3);
        int settleMs = ParseEnv("PERF_SINGLE_SETTLE_MS", 300);
        int durationSeconds = ParseEnv("PERF_SINGLE_DURATION_SECONDS", 5);
        int drainMs = ParseEnv("PERF_SINGLE_DRAIN_MS", 300);
        int latCount = ParseEnv("PERF_LAT_COUNT", 200);

        using var ctx = new Context();
        ApplySingleContextOptions(ctx);
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
            ApplySingleSocketOptions(router);
            router.SetOption(SocketOptions.RcvTimeo, 5000);

            gateway = new Gateway(ctx, discovery);
            if (!WaitUntil(() => discovery.ReceiverCount(service) > 0, 5000))
                return 2;
            if (!WaitUntil(() => gateway.ConnectionCount(service) > 0, 5000))
                return 2;
            string targetRoutingId = string.Empty;
            if (!WaitUntil(() =>
                {
                    try
                    {
                        var receivers = discovery.GetReceivers(service);
                        if (receivers.Length == 0)
                            return false;
                        if (string.IsNullOrWhiteSpace(receivers[0].RoutingId))
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

            var warmupDeadline = DateTime.UtcNow.AddSeconds(Math.Max(0,
                warmupSeconds));
            while (DateTime.UtcNow < warmupDeadline)
            {
                gateway.Send(service, payload.AsSpan(), SendFlags.None);
                GatewayReceiveProviderMessage(router, rid.AsSpan(),
                    data.AsSpan());
            }

            Thread.Sleep(Math.Max(0, settleMs));

            int recvCount = 0;
            var sw = System.Diagnostics.Stopwatch.StartNew();
            var throughputDeadline = DateTime.UtcNow.AddSeconds(
                Math.Max(1, durationSeconds));
            while (DateTime.UtcNow < throughputDeadline)
            {
                gateway.Send(service, payload.AsSpan(), SendFlags.None);
                GatewayReceiveProviderMessage(router, rid.AsSpan(), data.AsSpan());
                recvCount++;
            }
            sw.Stop();
            double thr = recvCount > 0 && sw.Elapsed.TotalSeconds > 0.0
                ? recvCount / sw.Elapsed.TotalSeconds
                : 0.0;

            Thread.Sleep(Math.Max(0, drainMs));

            int latRecv = 0;
            sw.Restart();
            for (int i = 0; i < latCount; i++)
            {
                gateway.Send(service, payload.AsSpan(), SendFlags.None);
                GatewayReceiveProviderMessage(router, rid.AsSpan(),
                    data.AsSpan());
                latRecv++;
            }
            sw.Stop();
            double latUs = (sw.Elapsed.TotalMilliseconds * 1000.0)
                / Math.Max(1, latRecv);
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
