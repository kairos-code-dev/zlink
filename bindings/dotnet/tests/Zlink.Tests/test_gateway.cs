using System;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace Zlink.Tests;

public sealed class test_gateway
{
    private static void RegisterProvider(Receiver receiver, Socket router,
        string regRouter, string serviceName, uint weight, string routingId,
        string transport = "tcp")
    {
        receiver.Bind(CoreTestSupport.NewEndpoint(transport, serviceName));
        router.SetOption(SocketOption.ProbeRouter, 1);
        router.SetOption(SocketOption.RoutingId, routingId);
        string advertise = router.GetOptionString(SocketOption.LastEndpoint);
        receiver.ConnectRegistry(regRouter);
        receiver.Register(serviceName, advertise, weight);
    }

    private static string ResolveRepoPath(string relativePath)
    {
        DirectoryInfo? current = new DirectoryInfo(Directory.GetCurrentDirectory());
        for (int i = 0; i < 10 && current != null; i++)
        {
            string candidate = Path.Combine(current.FullName, relativePath);
            if (File.Exists(candidate))
                return candidate;
            current = current.Parent;
        }
        throw new FileNotFoundException($"{relativePath} not found.");
    }

    [Fact]
    public void gateway_receiver_setsockopt_roundtrip()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-reg-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-reg-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        using var gateway = new Gateway(ctx, discovery);
        using var receiver = new Receiver(ctx);

        const int hwm = 1000000;
        gateway.SetOption(SocketOption.SndHwm, hwm);
        gateway.SetOption(SocketOption.RcvHwm, hwm);
        receiver.SetOption(ReceiverSocketRole.Router, SocketOption.SndHwm, hwm);
        receiver.SetOption(ReceiverSocketRole.Router, SocketOption.RcvHwm, hwm);

        using var gatewayRouter = gateway.CreateRouterSocket();
        using var receiverRouter = receiver.CreateRouterSocket();

        Assert.Equal(hwm, gatewayRouter.GetOption(SocketOption.SndHwm));
        Assert.Equal(hwm, gatewayRouter.GetOption(SocketOption.RcvHwm));
        Assert.Equal(hwm, receiverRouter.GetOption(SocketOption.SndHwm));
        Assert.Equal(hwm, receiverRouter.GetOption(SocketOption.RcvHwm));
    }

    [Fact]
    public void gateway_send_to_routing_id_delivers_payload()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw2-reg-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw2-reg-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe("svc");

        using var receiver = new Receiver(ctx);
        string receiverBind = CoreTestSupport.NewEndpoint("tcp", "gw-receiver");
        receiver.Bind(receiverBind);
        using var receiverRouter = receiver.CreateRouterSocket();
        string advertise = receiverRouter.GetOptionString(SocketOption.LastEndpoint);
        receiver.ConnectRegistry(regRouter);
        receiver.Register("svc", advertise, 1);

        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            var r = receiver.GetRegisterResult("svc");
            return r.Status == 0;
        }, 4000));

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc") > 0, 4000));

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc") > 0, 4000));

        ReceiverInfoRecord[] providers = discovery.GetReceivers("svc");
        Assert.NotEmpty(providers);
        byte[] targetRoutingId = providers[0].RoutingId;
        Assert.NotEmpty(targetRoutingId);

        gateway.SendToRoutingId("svc", targetRoutingId,
            Encoding.UTF8.GetBytes("rid-msg"), SendFlags.None);

        byte[] rid = CoreTestSupport.ReceiveBytesWithTimeout(receiverRouter, 256,
            2000);
        Assert.NotEmpty(rid);
        Assert.Equal("rid-msg",
            CoreTestSupport.ReceiveStringWithTimeout(receiverRouter, 256, 2000));
    }

    [Fact]
    public void gateway_single_service_tcp_routes_payload()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw3-reg-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw3-reg-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe("svc");

        using var receiver = new Receiver(ctx);
        using var receiverRouter = receiver.CreateRouterSocket();
        RegisterProvider(receiver, receiverRouter, regRouter, "svc", 1, "PROV1");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc") > 0, 4000));

        gateway.Send("svc", Encoding.UTF8.GetBytes("hello"), SendFlags.None);
        string payload = CoreTestSupport.ReceiveRouterPayloadWithTimeout(
            receiverRouter, "hello", 3000);
        Assert.Equal("hello", payload);
    }

    [Fact]
    public void gateway_send_message_moves_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-msg-own-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-msg-own-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe("svc-own");

        using var receiver = new Receiver(ctx);
        using var receiverRouter = receiver.CreateRouterSocket();
        RegisterProvider(receiver, receiverRouter, regRouter, "svc-own", 1, "OWN1");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-own") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-own") > 0, 4000));

        using var message = Message.FromBytes("owned-send"u8);
        gateway.Send("svc-own", new[] { message }, SendFlags.None);
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = message.Size;
        });

        string payload = CoreTestSupport.ReceiveRouterPayloadWithTimeout(
            receiverRouter, "owned-send", 3000);
        Assert.Equal("owned-send", payload);
    }

    [Fact]
    public void gateway_send_message_failure_still_consumes_ownership()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-msg-fail-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-msg-fail-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe("svc-missing");

        using var gateway = new Gateway(ctx, discovery);
        using var message = Message.FromBytes("owned-fail"u8);

        Assert.Throws<ZlinkException>(() =>
            gateway.Send("svc-missing", new[] { message }, SendFlags.DontWait));
        Assert.Throws<ObjectDisposedException>(() =>
        {
            _ = message.Size;
        });
    }

    [Fact]
    public void gateway_multi_service_tcp_routes_to_each_provider()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw4-reg-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw4-reg-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe("svc-A");
        discovery.Subscribe("svc-B");

        using var providerA = new Receiver(ctx);
        using var routerA = providerA.CreateRouterSocket();
        RegisterProvider(providerA, routerA, regRouter, "svc-A", 1, "PROVA");

        using var providerB = new Receiver(ctx);
        using var routerB = providerB.CreateRouterSocket();
        RegisterProvider(providerB, routerB, regRouter, "svc-B", 1, "PROVB");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-A") == 1, 4000));
        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-B") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-A") > 0, 4000));
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-B") > 0, 4000));

        gateway.Send("svc-A", Encoding.UTF8.GetBytes("msg-to-A"), SendFlags.None);
        gateway.Send("svc-B", Encoding.UTF8.GetBytes("msg-to-B"), SendFlags.None);

        Assert.Equal("msg-to-A", CoreTestSupport.ReceiveRouterPayloadWithTimeout(
            routerA, "msg-to-A", 3000));
        Assert.Equal("msg-to-B", CoreTestSupport.ReceiveRouterPayloadWithTimeout(
            routerB, "msg-to-B", 3000));
    }

    [Fact]
    public void gateway_refresh_on_update_routes_to_new_provider()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw5-reg-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw5-reg-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe("svc-update");

        using var provider1 = new Receiver(ctx);
        using var router1 = provider1.CreateRouterSocket();
        RegisterProvider(provider1, router1, regRouter, "svc-update", 1, "P1");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-update") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-update") > 0, 4000));

        CoreTestSupport.SendGatewayWithRetry(gateway, "svc-update", "old"u8, 2000);
        Assert.Equal("old", CoreTestSupport.ReceiveRouterPayloadWithTimeout(
            router1, "old", 3000));

        provider1.Unregister("svc-update");
        provider1.Dispose();
        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-update") == 0, 4000));

        using var provider2 = new Receiver(ctx);
        using var router2 = provider2.CreateRouterSocket();
        RegisterProvider(provider2, router2, regRouter, "svc-update", 1, "P2");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-update") == 1, 4000));
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-update") > 0, 4000));

        CoreTestSupport.SendGatewayWithRetry(gateway, "svc-update", "new"u8, 2000);
        Assert.Equal("new", CoreTestSupport.ReceiveRouterPayloadWithTimeout(
            router2, "new", 3000));
    }

    [Fact]
    public void gateway_protocol_ws()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported("ws"))
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-ws-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-ws-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe("svc-ws");

        using var provider = new Receiver(ctx);
        using var providerRouter = provider.CreateRouterSocket();
        RegisterProvider(provider, providerRouter, regRouter, "svc-ws", 1, "PROVWS",
            "ws");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-ws") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-ws") > 0, 4000));

        CoreTestSupport.SendGatewayWithRetry(gateway, "svc-ws", "ws-test"u8, 2000);
        Assert.Equal("ws-test", CoreTestSupport.ReceiveRouterPayloadWithTimeout(
            providerRouter, "ws-test", 3000));
    }

    [Fact]
    public void gateway_protocol_tls()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported("tls"))
            return;

        string caCert = ResolveRepoPath("core/tests/certs/gen/ca.crt");
        string serverCert = ResolveRepoPath("core/tests/certs/gen/server.crt");
        string serverKey = ResolveRepoPath("core/tests/certs/gen/server.key");

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-tls-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-tls-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe("svc-tls");

        using var provider = new Receiver(ctx);
        provider.SetTlsServer(serverCert, serverKey);
        using var providerRouter = provider.CreateRouterSocket();
        RegisterProvider(provider, providerRouter, regRouter, "svc-tls", 1,
            "PROVTLS", "tls");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-tls") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        gateway.SetTlsClient(caCert, "localhost", false);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-tls") > 0, 5000));

        CoreTestSupport.SendGatewayWithRetry(gateway, "svc-tls", "tls-test"u8, 3000);
        Assert.Equal("tls-test", CoreTestSupport.ReceiveRouterPayloadWithTimeout(
            providerRouter, "tls-test", 4000));
    }

    [Fact]
    public void gateway_protocol_wss()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;
        if (!CoreTestSupport.IsTransportSupported("wss"))
            return;

        string caCert = ResolveRepoPath("core/tests/certs/gen/ca.crt");
        string serverCert = ResolveRepoPath("core/tests/certs/gen/server.crt");
        string serverKey = ResolveRepoPath("core/tests/certs/gen/server.key");

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-wss-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-wss-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe("svc-wss");

        using var provider = new Receiver(ctx);
        provider.SetTlsServer(serverCert, serverKey);
        using var providerRouter = provider.CreateRouterSocket();
        RegisterProvider(provider, providerRouter, regRouter, "svc-wss", 1,
            "PROVWSS", "wss");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-wss") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        gateway.SetTlsClient(caCert, "localhost", false);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-wss") > 0, 5000));

        CoreTestSupport.SendGatewayWithRetry(gateway, "svc-wss", "wss-test"u8, 3000);
        Assert.Equal("wss-test", CoreTestSupport.ReceiveRouterPayloadWithTimeout(
            providerRouter, "wss-test", 4000));
    }

    [Fact]
    public void gateway_load_balancing_multiple_providers()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-lb-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-lb-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        const string serviceName = "lb-svc";
        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe(serviceName);

        using var provider1 = new Receiver(ctx);
        using var router1 = provider1.CreateRouterSocket();
        RegisterProvider(provider1, router1, regRouter, serviceName, 10, "PROV1");

        using var provider2 = new Receiver(ctx);
        using var router2 = provider2.CreateRouterSocket();
        RegisterProvider(provider2, router2, regRouter, serviceName, 10, "PROV2");

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount(serviceName) == 2, 6000));

        const int numMessages = 20;
        for (int i = 0; i < numMessages; i++)
            CoreTestSupport.SendGatewayWithRetry(gateway, serviceName, "lb"u8, 2000);

        int recv1 = 0;
        int recv2 = 0;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            if (CoreTestSupport.TryReceiveMultipartLastPart(router1, 256, out _))
                recv1++;
            if (CoreTestSupport.TryReceiveMultipartLastPart(router2, 256, out _))
                recv2++;
            return recv1 + recv2 >= numMessages;
        }, 6000, 2));

        Assert.Equal(numMessages, recv1 + recv2);
    }

    [Fact]
    public void gateway_weighted_load_balancing_multiple_providers()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-wlb-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-wlb-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        const string serviceName = "lb-weighted";
        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe(serviceName);

        using var provider1 = new Receiver(ctx);
        using var router1 = provider1.CreateRouterSocket();
        RegisterProvider(provider1, router1, regRouter, serviceName, 8, "WPROV1");

        using var provider2 = new Receiver(ctx);
        using var router2 = provider2.CreateRouterSocket();
        RegisterProvider(provider2, router2, regRouter, serviceName, 1, "WPROV2");

        using var gateway = new Gateway(ctx, discovery);
        gateway.SetLoadBalancing(serviceName, GatewayLoadBalancing.Weighted);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount(serviceName) == 2, 6000));

        const int numMessages = 36;
        for (int i = 0; i < numMessages; i++)
            CoreTestSupport.SendGatewayWithRetry(gateway, serviceName, "wlb"u8, 2000);

        int recv1 = 0;
        int recv2 = 0;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            if (CoreTestSupport.TryReceiveMultipartLastPart(router1, 256, out _))
                recv1++;
            if (CoreTestSupport.TryReceiveMultipartLastPart(router2, 256, out _))
                recv2++;
            return recv1 + recv2 >= numMessages;
        }, 8000, 2));

        Assert.Equal(numMessages, recv1 + recv2);
        Assert.True(recv1 > 0);
        Assert.True(recv2 > 0);
        Assert.NotEqual(recv1, recv2);
    }

    [Fact]
    public async Task gateway_concurrent_send_and_updates()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-sync-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-sync-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        const string serviceName = "svc-sync";
        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regPub);
        discovery.Subscribe(serviceName);

        using var provider = new Receiver(ctx);
        using var router = provider.CreateRouterSocket();
        RegisterProvider(provider, router, regRouter, serviceName, 1, "SYNC1");

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount(serviceName) > 0, 5000));

        const int workers = 4;
        const int perWorker = 25;
        var cts = new CancellationTokenSource();
        Task[] tasks = new Task[workers + 1];

        for (int i = 0; i < workers; i++)
        {
            tasks[i] = Task.Run(() =>
            {
                for (int n = 0; n < perWorker; n++)
                    CoreTestSupport.SendGatewayWithRetry(gateway, serviceName,
                        "sync"u8, 3000);
            });
        }

        tasks[workers] = Task.Run(() =>
        {
            while (!cts.IsCancellationRequested)
            {
                provider.UpdateWeight(serviceName, 1);
                provider.UpdateWeight(serviceName, 5);
            }
        });

        await Task.WhenAll(tasks.AsSpan(0, workers).ToArray());
        cts.Cancel();
        await tasks[workers];

        int expected = workers * perWorker;
        int received = 0;
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            if (CoreTestSupport.TryReceiveMultipartLastPart(router, 256, out _))
                received++;
            return received >= expected;
        }, 10000, 1));

        Assert.Equal(expected, received);
    }
}
