using System;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_socket_options
{
    [Fact]
    public void socket_options_runtime_int_and_long_roundtrip()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var stream = ctx.CreateStreamSocket();
        using var dealer = ctx.CreateDealerSocket();

        stream.Options.Notify = false;
        Assert.False(stream.Options.Notify);

        stream.Options.MaxMessageSize = 1024L;
        Assert.Equal(1024L, stream.Options.MaxMessageSize);

        RoutingId dealerRid = CoreTestSupport.RoutingIdUtf8("RID-OPT");
        dealer.SetRoutingId(dealerRid);
        Assert.Equal(dealerRid, dealer.GetRoutingId());
    }

    [Fact]
    public void socket_options_runtime_string_getter_works()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var router = ctx.CreateRouterSocket();

        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "socket-options-last-endpoint");
        router.Bind(endpoint);

        string actual = router.Options.LastEndpoint;
        Assert.StartsWith("tcp://", actual);
    }

    [Fact]
    public void typed_socket_option_helpers_route_to_supported_options()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var dealer = ctx.CreateDealerSocket();
        using var router = ctx.CreateRouterSocket();
        using var stream = ctx.CreateStreamSocket();
        using var xpub = ctx.CreateXPubSocket();

        RoutingId dealerRid = CoreTestSupport.RoutingIdUtf8("DEALER-RID");
        dealer.SetRoutingId(dealerRid);
        Assert.Equal(dealerRid, dealer.GetRoutingId());

        RoutingId routerRid = CoreTestSupport.RoutingIdUtf8("ROUTER-RID");
        router.SetRoutingId(routerRid);
        Assert.Equal(routerRid, router.GetRoutingId());
        router.Options.Mandatory = true;
        Assert.True(router.Options.Mandatory);

        stream.Options.Notify = true;
        Assert.True(stream.Options.Notify);
        RoutingId streamRid = CoreTestSupport.RoutingIdUtf8("STRM");
        stream.SetRoutingId(streamRid);
        Assert.Equal(streamRid, stream.GetRoutingId());

        xpub.Options.Verbose = true;
        xpub.Options.Verboser = true;
        xpub.Options.NoDrop = true;
    }

    [Fact]
    public void peer_weight_options_validate_documented_range()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var dealer = ctx.CreateDealerSocket();
        using var router = ctx.CreateRouterSocket();

        dealer.Options.PeerWeight = 0;
        dealer.Options.PeerWeight = 100;
        router.Options.PeerWeight = 0;
        Assert.Equal(0, router.Options.PeerWeight);
        router.Options.PeerWeight = 100;
        Assert.Equal(100, router.Options.PeerWeight);

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            dealer.Options.PeerWeight = -1);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            dealer.Options.PeerWeight = 101);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            router.Options.PeerWeight = -1);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            router.Options.PeerWeight = 101);
    }

    [Fact]
    public void subscription_at_returns_null_when_index_is_absent()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var sub = ctx.CreateSubSocket();
        using var node = ctx.CreateSpotNode();
        using var spot = node.CreateSpot();

        Assert.Null(sub.SubscriptionAt(0));
        Assert.Null(spot.SubscriptionAt(0));

        sub.SetSubscription("prices");
        spot.SetSubscription("prices");

        Assert.Equal("prices", sub.SubscriptionAt(0)?.Filter);
        Assert.Equal("prices", spot.SubscriptionAt(0)?.Filter);
    }

    [Fact]
    public void spot_node_dispatch_workers_validate_min_max_contract()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();

        Assert.True(node.DispatchWorkersMin >= 1);
        Assert.True(node.DispatchWorkersMax >= node.DispatchWorkersMin);

        node.DispatchWorkersMin = 1;
        node.DispatchWorkersMax = 1;
        Assert.Equal(1, node.DispatchWorkersMin);
        Assert.Equal(1, node.DispatchWorkersMax);

        Assert.Throws<ArgumentOutOfRangeException>(() =>
            node.DispatchWorkersMin = 0);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            node.DispatchWorkersMax = 0);
        node.DispatchWorkersMin = 2;
        Assert.True(node.DispatchWorkersMax >= node.DispatchWorkersMin);
        Assert.Throws<ArgumentOutOfRangeException>(() =>
            node.DispatchWorkersMax = 1);
    }

    [Fact]
    public void spot_lookup_returns_null_when_spot_is_absent()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();

        Assert.Null(node.SpotLookup(CoreTestSupport.RoutingIdUtf8("missing")));
    }

    [Fact]
    public void spot_node_get_or_create_spot_reuses_logical_spot()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = Zlink.CreateContext();
        using var node = ctx.CreateSpotNode();
        var spotRid = CoreTestSupport.RoutingIdUtf8("managed-room");

        using var first = node.GetOrCreateSpot(spotRid, out var firstCreated);
        using var second = node.GetOrCreateSpot(spotRid, out var secondCreated);

        Assert.True(firstCreated);
        Assert.False(secondCreated);
        Assert.Equal(spotRid, first.RoutingId);
        Assert.Equal(spotRid, second.RoutingId);
    }

}
