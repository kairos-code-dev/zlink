using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace Zlink.Tests;

public sealed class test_gateway
{
    private static void RegisterProvider(Receiver receiver, string regRouter,
        string serviceName, uint weight, string routingId,
        string transport = "tcp")
    {
        receiver.SetRoutingId(routingId);
        receiver.Bind(CoreTestSupport.NewEndpoint(transport, serviceName));
        string advertise = receiver.GetLastEndpoint();
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
    public void gateway_receiver_setoption_accepts_supported_values()
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
        discovery.ConnectRegistry(regRouter);
        using var gateway = new Gateway(ctx, discovery);
        using var receiver = new Receiver(ctx);

        const int hwm = 1000000;
        gateway.SetOption(SocketOptions.SndHwm, hwm);
        gateway.SetOption(SocketOptions.RcvHwm, hwm);
        receiver.SetOption(SocketOptions.SndHwm, hwm);
        receiver.SetOption(SocketOptions.RcvHwm, hwm);
    }

    [Fact]
    public void gateway_and_receiver_router_handles_are_not_exposed()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        using var gateway = new Gateway(ctx, discovery);
        using var receiver = new Receiver(ctx);

        ZlinkException gw = Assert.Throws<ZlinkException>(() =>
            gateway.CreateRouterSocket());
        Assert.Equal(ErrorCode.ENotSup, ZlinkException.MapErrorCode(gw.Errno));

        ZlinkException rcv = Assert.Throws<ZlinkException>(() =>
            receiver.CreateRouterSocket());
        Assert.Equal(ErrorCode.ENotSup, ZlinkException.MapErrorCode(rcv.Errno));
    }

    [Fact]
    public void gateway_argument_validation_managed()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        using var gateway = new Gateway(ctx, discovery);

        Assert.Throws<ArgumentException>(() =>
            gateway.Send("", "x"u8, SendFlags.None));
        Assert.Throws<ArgumentException>(() =>
            gateway.ConnectionCount(""));
        Assert.Throws<ArgumentException>(() =>
            gateway.SetLoadBalancing("", GatewayLoadBalancing.RoundRobin));
        Assert.Throws<ArgumentException>(() =>
            gateway.SendToRoutingId("", "RID", "x"u8, SendFlags.None));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            gateway.SendToRoutingId("svc", "", "x"u8, SendFlags.None));

        Assert.Throws<ArgumentException>(() =>
            _ = new Gateway(ctx, discovery, ""));
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            _ = new Gateway(ctx, discovery, new string('r', 256)));
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
        discovery.ConnectRegistry(regRouter);

        using var receiver = new Receiver(ctx);
        const string targetRoutingId = "RID-STR-1";
        RegisterProvider(receiver, regRouter, "svc", 1, targetRoutingId);

        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            ReceiverRegisterResult r = receiver.GetRegisterResult("svc");
            return r.Status == 0;
        }, 4000));

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc") > 0, 4000));

        ReceiverInfoRecord[] providers = discovery.GetReceivers("svc");
        Assert.Single(providers);
        Assert.Equal(targetRoutingId, providers[0].RoutingId);

        gateway.SendToRoutingId("svc", targetRoutingId, "rid-msg"u8,
            SendFlags.None);

        string payload = CoreTestSupport.ReceiveReceiverPayloadWithTimeout(
            receiver, "rid-msg", 3000);
        Assert.Equal("rid-msg", payload);
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
        discovery.ConnectRegistry(regRouter);

        using var receiver = new Receiver(ctx);
        RegisterProvider(receiver, regRouter, "svc", 1, "PROV1");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc") > 0, 4000));

        gateway.Send("svc", "hello"u8, SendFlags.None);
        string payload = CoreTestSupport.ReceiveReceiverPayloadWithTimeout(
            receiver, "hello", 3000);
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
        discovery.ConnectRegistry(regRouter);

        using var receiver = new Receiver(ctx);
        RegisterProvider(receiver, regRouter, "svc-own", 1, "OWN1");

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

        string payload = CoreTestSupport.ReceiveReceiverPayloadWithTimeout(
            receiver, "owned-send", 3000);
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
        discovery.ConnectRegistry(regRouter);

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
        discovery.ConnectRegistry(regRouter);

        using var providerA = new Receiver(ctx);
        RegisterProvider(providerA, regRouter, "svc-A", 1, "PROVA");

        using var providerB = new Receiver(ctx);
        RegisterProvider(providerB, regRouter, "svc-B", 1, "PROVB");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-A") == 1, 4000));
        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-B") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-A") > 0, 4000));
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-B") > 0, 4000));

        gateway.Send("svc-A", "msg-to-A"u8, SendFlags.None);
        gateway.Send("svc-B", "msg-to-B"u8, SendFlags.None);

        Assert.Equal("msg-to-A", CoreTestSupport.ReceiveReceiverPayloadWithTimeout(
            providerA, "msg-to-A", 3000));
        Assert.Equal("msg-to-B", CoreTestSupport.ReceiveReceiverPayloadWithTimeout(
            providerB, "msg-to-B", 3000));
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
        discovery.ConnectRegistry(regRouter);

        using var provider1 = new Receiver(ctx);
        RegisterProvider(provider1, regRouter, "svc-update", 1, "P1");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-update") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-update") > 0, 4000));

        CoreTestSupport.SendGatewayWithRetry(gateway, "svc-update", "old"u8, 2000);
        Assert.Equal("old", CoreTestSupport.ReceiveReceiverPayloadWithTimeout(
            provider1, "old", 3000));

        provider1.Unregister("svc-update");
        provider1.Dispose();
        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-update") == 0, 4000));

        using var provider2 = new Receiver(ctx);
        RegisterProvider(provider2, regRouter, "svc-update", 1, "P2");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-update") == 1, 4000));
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-update") > 0, 4000));

        CoreTestSupport.SendGatewayWithRetry(gateway, "svc-update", "new"u8, 2000);
        Assert.Equal("new", CoreTestSupport.ReceiveReceiverPayloadWithTimeout(
            provider2, "new", 3000));
    }

    [Fact]
    public void gateway_protocol_ws()
    {
        if (!CoreTestSupport.IsNativeAvailable() || !CoreTestSupport.IsTransportSupported("ws"))
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-ws-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-ws-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regRouter);

        using var provider = new Receiver(ctx);
        RegisterProvider(provider, regRouter, "svc-ws", 1, "PROVWS", "ws");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-ws") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-ws") > 0, 4000));

        CoreTestSupport.SendGatewayWithRetry(gateway, "svc-ws", "ws-test"u8, 2000);
        Assert.Equal("ws-test", CoreTestSupport.ReceiveReceiverPayloadWithTimeout(
            provider, "ws-test", 3000));
    }

    [Fact]
    public void gateway_protocol_tls()
    {
        if (!CoreTestSupport.IsNativeAvailable() || !CoreTestSupport.IsTransportSupported("tls"))
            return;

        string caCert = ResolveRepoPath("bindings/dotnet/tests/certs/ca.crt");
        string serverCert = ResolveRepoPath("bindings/dotnet/tests/certs/server.crt");
        string serverKey = ResolveRepoPath("bindings/dotnet/tests/certs/server.key");

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-tls-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-tls-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regRouter);

        using var provider = new Receiver(ctx);
        provider.SetTlsServer(serverCert, serverKey);
        RegisterProvider(provider, regRouter, "svc-tls", 1, "PROVTLS", "tls");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-tls") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        gateway.SetTlsClient(caCert, "localhost", false);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-tls") > 0, 5000));

        CoreTestSupport.SendGatewayWithRetry(gateway, "svc-tls", "tls-test"u8, 3000);
        Assert.Equal("tls-test", CoreTestSupport.ReceiveReceiverPayloadWithTimeout(
            provider, "tls-test", 4000));
    }

    [Fact]
    public void gateway_protocol_wss()
    {
        if (!CoreTestSupport.IsNativeAvailable() || !CoreTestSupport.IsTransportSupported("wss"))
            return;

        string caCert = ResolveRepoPath("bindings/dotnet/tests/certs/ca.crt");
        string serverCert = ResolveRepoPath("bindings/dotnet/tests/certs/server.crt");
        string serverKey = ResolveRepoPath("bindings/dotnet/tests/certs/server.key");

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "gw-wss-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "gw-wss-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regRouter);

        using var provider = new Receiver(ctx);
        provider.SetTlsServer(serverCert, serverKey);
        RegisterProvider(provider, regRouter, "svc-wss", 1, "PROVWSS", "wss");

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-wss") == 1, 4000));

        using var gateway = new Gateway(ctx, discovery);
        gateway.SetTlsClient(caCert, "localhost", false);
        Assert.True(CoreTestSupport.WaitUntil(() =>
            gateway.ConnectionCount("svc-wss") > 0, 5000));

        CoreTestSupport.SendGatewayWithRetry(gateway, "svc-wss", "wss-test"u8, 3000);
        Assert.Equal("wss-test", CoreTestSupport.ReceiveReceiverPayloadWithTimeout(
            provider, "wss-test", 4000));
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
        discovery.ConnectRegistry(regRouter);

        using var provider1 = new Receiver(ctx);
        RegisterProvider(provider1, regRouter, serviceName, 10, "PROV1");

        using var provider2 = new Receiver(ctx);
        RegisterProvider(provider2, regRouter, serviceName, 10, "PROV2");

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
            if (CoreTestSupport.TryReceiveReceiverMultipartLastPart(provider1, 256,
                    out _))
                recv1++;
            if (CoreTestSupport.TryReceiveReceiverMultipartLastPart(provider2, 256,
                    out _))
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
        discovery.ConnectRegistry(regRouter);

        using var provider1 = new Receiver(ctx);
        RegisterProvider(provider1, regRouter, serviceName, 8, "WPROV1");

        using var provider2 = new Receiver(ctx);
        RegisterProvider(provider2, regRouter, serviceName, 1, "WPROV2");

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
            if (CoreTestSupport.TryReceiveReceiverMultipartLastPart(provider1, 256,
                    out _))
                recv1++;
            if (CoreTestSupport.TryReceiveReceiverMultipartLastPart(provider2, 256,
                    out _))
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
        discovery.ConnectRegistry(regRouter);

        using var provider = new Receiver(ctx);
        RegisterProvider(provider, regRouter, serviceName, 1, "SYNC1");

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
            if (CoreTestSupport.TryReceiveReceiverMultipartLastPart(provider, 256,
                    out _))
                received++;
            return received >= expected;
        }, 10000, 1));

        Assert.Equal(expected, received);
    }
}
