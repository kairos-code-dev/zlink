using System;
using Xunit;

namespace Zlink.Tests;

public sealed class test_service_discovery
{
    private static string RegisterProvider(Receiver provider, string regRouter,
        string serviceName, uint weight)
    {
        provider.Bind(CoreTestSupport.NewEndpoint("tcp", serviceName));
        string advertise = provider.GetLastEndpoint();
        provider.ConnectRegistry(regRouter);
        provider.Register(serviceName, advertise, weight);
        return advertise;
    }

    [Fact]
    public void service_argument_validation_managed()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        Assert.Throws<ArgumentException>(() => discovery.ConnectRegistry(""));
        Assert.Throws<ArgumentException>(() => discovery.ReceiverCount(""));
        Assert.Throws<ArgumentException>(() => discovery.ServiceAvailable(""));
        Assert.Throws<ArgumentException>(() => discovery.GetReceivers(""));

        using var receiver = new Receiver(ctx);
        Assert.Throws<ArgumentException>(() => receiver.Bind(""));
        Assert.Throws<ArgumentException>(() => receiver.ConnectRegistry(""));
        Assert.Throws<ArgumentException>(() => receiver.Register("", "tcp://x", 1));
        Assert.Throws<ArgumentException>(() => receiver.Register("svc", "", 1));
        Assert.Throws<ArgumentException>(() => receiver.UpdateWeight("", 1));
        Assert.Throws<ArgumentException>(() => receiver.Unregister(""));
        Assert.Throws<ArgumentException>(() => receiver.GetRegisterResult(""));
        Assert.Throws<ArgumentException>(() => receiver.SetTlsServer("", "key"));
        Assert.Throws<ArgumentException>(() => receiver.SetTlsServer("cert", ""));
        Assert.Throws<ArgumentException>(() => _ = new Receiver(ctx, ""));

        using var registry = new Registry(ctx);
        Assert.Throws<ArgumentException>(() => registry.SetEndpoints("", "inproc://r"));
        Assert.Throws<ArgumentException>(() => registry.SetEndpoints("inproc://p", ""));
        Assert.Throws<ArgumentException>(() => registry.AddPeer(""));

        using var spotNode = new SpotNode(ctx);
        Assert.Throws<ArgumentException>(() => spotNode.Bind(""));
        Assert.Throws<ArgumentException>(() => spotNode.ConnectPeerPub(""));
        Assert.Throws<ArgumentException>(() => spotNode.DisconnectPeerPub(""));
        Assert.Throws<ArgumentException>(() => spotNode.Register("", "tcp://x"));
        Assert.Throws<ArgumentException>(() => spotNode.Register("svc", ""));
        Assert.Throws<ArgumentException>(() => spotNode.Unregister(""));
        Assert.Throws<ArgumentException>(() => spotNode.SetTlsServer("", "key"));
        Assert.Throws<ArgumentException>(() => spotNode.SetTlsServer("cert", ""));
        Assert.Throws<ArgumentException>(() =>
            spotNode.SetTlsClient("", "host", trustSystem: false));
        Assert.Throws<ArgumentException>(() =>
            spotNode.SetTlsClient("ca", "", trustSystem: false));
    }

    [Fact]
    public void discovery_provider_registration_and_filtering()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);

        string regPub = CoreTestSupport.NewEndpoint("inproc", "reg-pub");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "reg-router");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regRouter);
        using var providerA = new Receiver(ctx);
        string bindA = CoreTestSupport.NewEndpoint("tcp", "svc-a");
        providerA.Bind(bindA);
        string advertiseA = providerA.GetLastEndpoint();
        providerA.ConnectRegistry(regRouter);
        providerA.Register("svc-A", advertiseA, 10);

        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            var result = providerA.GetRegisterResult("svc-A");
            return result.Status == 0;
        }, 3000));

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-A") == 1, 3000));

        ReceiverInfoRecord[] providers = discovery.GetReceivers("svc-A");
        Assert.Single(providers);
        Assert.Equal("svc-A", providers[0].ServiceName);
        Assert.Equal(advertiseA, providers[0].Endpoint);
        Assert.Equal((uint)10, providers[0].Weight);
        Assert.NotEmpty(providers[0].RoutingId);

        providerA.Unregister("svc-A");
        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-A") == 0, 3000));
    }

    [Fact]
    public void discovery_reports_multiple_services()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "reg-pub-filter");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "reg-router-filter");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regRouter);
        using var providerA = new Receiver(ctx);
        using var providerB = new Receiver(ctx);
        string advertiseA = RegisterProvider(providerA, regRouter, "svc-A", 10);
        _ = RegisterProvider(providerB, regRouter, "svc-B", 20);

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-A") == 1, 3000));
        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("svc-B") == 1, 3000));

        ReceiverInfoRecord[] providersA = discovery.GetReceivers("svc-A");
        Assert.Single(providersA);
        Assert.Equal(advertiseA, providersA[0].Endpoint);
        Assert.Equal((uint)10, providersA[0].Weight);

        ReceiverInfoRecord[] providersB = discovery.GetReceivers("svc-B");
        Assert.Single(providersB);
        Assert.Equal((uint)20, providersB[0].Weight);
    }

    [Fact]
    public void discovery_heartbeat_timeout_removes_provider()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "reg-pub-hb");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "reg-router-hb");
        registry.SetEndpoints(regPub, regRouter);
        registry.SetHeartbeat(100, 500);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regRouter);
        using var provider = new Receiver(ctx);
        _ = RegisterProvider(provider, regRouter, "hb-svc", 1);

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("hb-svc") == 1, 3000));

        provider.Dispose();
        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("hb-svc") == 0, 4000));
    }

    [Fact]
    public void discovery_weight_update_propagates()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var registry = new Registry(ctx);
        string regPub = CoreTestSupport.NewEndpoint("inproc", "reg-pub-weight");
        string regRouter = CoreTestSupport.NewEndpoint("inproc", "reg-router-weight");
        registry.SetEndpoints(regPub, regRouter);
        registry.Start();

        using var discovery = new Discovery(ctx, DiscoveryServiceType.Gateway);
        discovery.ConnectRegistry(regRouter);
        using var provider = new Receiver(ctx);
        _ = RegisterProvider(provider, regRouter, "weight-svc", 10);

        Assert.True(CoreTestSupport.WaitUntil(() =>
            discovery.ReceiverCount("weight-svc") == 1, 3000));

        ReceiverInfoRecord[] before = discovery.GetReceivers("weight-svc");
        Assert.Single(before);
        Assert.Equal((uint)10, before[0].Weight);

        provider.UpdateWeight("weight-svc", 50);
        Assert.True(CoreTestSupport.WaitUntil(() =>
        {
            ReceiverInfoRecord[] now = discovery.GetReceivers("weight-svc");
            return now.Length == 1 && now[0].Weight == 50;
        }, 3000));
    }
}
