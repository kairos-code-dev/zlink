using System.Threading;
using Xunit;

namespace Zlink.Tests;

public sealed class test_discovery_router_socket
{
    [Fact]
    public async Task discovery_attached_routers_exchange_routed_request()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var registryContext = new Context();
        using var leftContext = new Context();
        using var rightContext = new Context();
        using var registry = new Registry(registryContext);
        using var leftDiscovery = new Discovery(leftContext, AutoConnectType.RouteMesh, "routed-discovery");
        using var rightDiscovery = new Discovery(rightContext, AutoConnectType.RouteMesh, "routed-discovery");
        var left = new RouterSocket(leftContext);
        var right = new RouterSocket(rightContext);

        var registryPub = CoreTestSupport.NewEndpoint("tcp", "routed-reg-pub");
        var registryRouter = CoreTestSupport.NewEndpoint("tcp", "routed-reg-router");
        var leftEndpoint = CoreTestSupport.NewEndpoint("tcp", "routed-left");
        var rightEndpoint = CoreTestSupport.NewEndpoint("tcp", "routed-right");
        var leftRid = RoutingId.FromString("01");
        var rightRid = RoutingId.FromString("02");

        registry.Bind(registryPub, registryRouter);
        registry.SetBroadcastInterval(50);
        leftDiscovery.ConnectRegistry(registryRouter);
        rightDiscovery.ConnectRegistry(registryRouter);

        left.RouterOptions.RoutingId = leftRid;
        right.RouterOptions.RoutingId = rightRid;
        left.AttachDiscovery(leftDiscovery);
        right.AttachDiscovery(rightDiscovery);
        left.Bind(leftEndpoint);
        right.Bind(rightEndpoint);

        var peersReady = CoreTestSupport.WaitUntil(
            () => registry.MemberPeers("routed-discovery").Length >= 2
                && leftDiscovery.MemberPeers().Length >= 1
                && rightDiscovery.MemberPeers().Length >= 1,
            timeoutMs: 10000,
            sleepMs: 25);
        Assert.True(
            peersReady,
            $"registry={registry.MemberPeers("routed-discovery").Length}, left={leftDiscovery.MemberPeers().Length}, right={rightDiscovery.MemberPeers().Length}");

        var received = new ManualResetEventSlim(false);
        var server = Task.Run(() =>
        {
            using var request = right.Recv();
            Assert.Equal("ping", request.Parts[0].GetString());
            using var reply = Message.FromString("pong");
            right.Reply(
                request.RoutingId ?? throw new InvalidOperationException("missing source rid"),
                request.RequestSeq ?? 0UL,
                reply);
            received.Set();
        });

        using var ping = Message.FromString("ping");
        var replies = await left.RequestAsync(rightRid, ping, TimeSpan.FromSeconds(3));
        try
        {
            Assert.Equal("pong", replies[0].GetString());
        }
        finally
        {
            foreach (var reply in replies)
            {
                reply.Dispose();
            }
        }

        Assert.True(received.Wait(TimeSpan.FromSeconds(3)));
        await server;
    }
}
