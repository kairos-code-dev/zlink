using System;
using System.Text;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_spot_router_channel_peer
{
    [Fact]
    public void legacy_router_channel_peer_apis_return_migration_error()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var router = ctx.CreateRouterSocket();

        router.Bind(CoreTestSupport.NewEndpoint("inproc", "legacy-router-channel"));
        string endpoint = router.Options.LastEndpoint;
        RoutingId rid = CoreTestSupport.RoutingIdUtf8("legacy-router");

#pragma warning disable CS0618
        Assert.Throws<ZlinkConnectException>(() =>
            node.ConnectRouterChannelPeer("api", endpoint));
        Assert.Throws<ZlinkConnectException>(() =>
            node.ConnectRouterChannelPeerRid("api", rid, endpoint));
        Assert.Throws<ZlinkConnectException>(() =>
            node.DisconnectRouterChannelPeer("api", endpoint));
        Assert.Throws<ZlinkConnectException>(() =>
            node.DisconnectRouterChannelPeerRid("api", rid));
#pragma warning restore CS0618
    }

    [Fact]
    public void spot_route_bridge_dealer_send_emits_relay_packet()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        using var dealer = ctx.CreateDealerSocket();
        using var router = ctx.CreateRouterSocket();
        using var bridge = node.CreateRouteBridge();

        string endpoint = CoreTestSupport.NewEndpoint("inproc", "bridge-dealer-egress");
        router.Bind(endpoint);
        dealer.Connect(endpoint);
        bridge.AttachDealerChannel("api", dealer);

        RoutingId spotRid = CoreTestSupport.RoutingIdUtf8("target-spot");
        using Message payload = Message.From("hello-bridge");
        Assert.True(bridge.Send("api", spotRid, new[] { payload }));

        using var received = Received.Create();
        Assert.True(CoreTestSupport.WaitUntil(
            () => router.Recv(received, RecvFlags.DontWait),
            timeoutMs: 5000));

        Assert.True(received.Parts.Count >= 3);
        Assert.Equal("__zlink.routed_spot.egress.send",
            Encoding.UTF8.GetString(received.Parts[0].AsReadOnlySpan()));
        Assert.Equal(spotRid.ToBytes(), received.Parts[1].ToArray());
        Assert.Equal("hello-bridge",
            Encoding.UTF8.GetString(received.Parts[2].AsReadOnlySpan()).Trim('\0'));

        SpotRouteBridgeSummary summary = bridge.Summary();
        Assert.Equal(1u, summary.AttachedChannelCount);
    }
}
