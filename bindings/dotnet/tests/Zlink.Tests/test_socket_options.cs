using System;
using System.Collections.Generic;
using System.Reflection;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_socket_options
{
    private static readonly HashSet<string> Int64Options = new()
    {
        nameof(SocketOptions.MaxMsgSize)
    };

    private static readonly HashSet<string> UInt64Options = new()
    {
        nameof(SocketOptions.Affinity)
    };

    private static readonly HashSet<string> ByteOptions = new()
    {
        nameof(SocketOptions.RoutingId),
        nameof(SocketOptions.ConnectRoutingId)
    };

    private static readonly HashSet<string> StringOptions = new()
    {
        nameof(SocketOptions.Subscribe),
        nameof(SocketOptions.Unsubscribe),
        nameof(SocketOptions.LastEndpoint),
        nameof(SocketOptions.XPubWelcomeMsg),
        nameof(SocketOptions.XPubApproveSubscribe),
        nameof(SocketOptions.XPubRejectSubscribe),
        nameof(SocketOptions.BindToDevice),
        nameof(SocketOptions.TlsCert),
        nameof(SocketOptions.TlsKey),
        nameof(SocketOptions.TlsCa),
        nameof(SocketOptions.TlsHostname),
        nameof(SocketOptions.TlsPassword)
    };

    [Fact]
    public void socket_options_cover_all_enum_names()
    {
        string[] enumNames = Enum.GetNames(typeof(SocketOption));
        foreach (string optionName in enumNames)
        {
            PropertyInfo? property = typeof(SocketOptions).GetProperty(optionName,
                BindingFlags.Public | BindingFlags.Static);
            Assert.NotNull(property);
        }
    }

    [Fact]
    public void socket_options_type_contract_matches_enum_members()
    {
        string[] enumNames = Enum.GetNames(typeof(SocketOption));
        foreach (string optionName in enumNames)
        {
            PropertyInfo property = typeof(SocketOptions).GetProperty(optionName,
                BindingFlags.Public | BindingFlags.Static)!;

            Type expectedType = typeof(SocketOptionKey<int>);
            if (Int64Options.Contains(optionName))
                expectedType = typeof(SocketOptionKey<long>);
            else if (UInt64Options.Contains(optionName))
                expectedType = typeof(SocketOptionKey<ulong>);
            else if (ByteOptions.Contains(optionName))
                expectedType = typeof(SocketOptionKey<byte[]>);
            else if (StringOptions.Contains(optionName))
                expectedType = typeof(SocketOptionKey<string>);

            Assert.Equal(expectedType, property.PropertyType);
        }
    }

    [Fact]
    public void socket_options_runtime_int_and_long_roundtrip()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new StreamSocket(ctx);
        using var dealer = new DealerSocket(ctx);

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

        using var ctx = new Context();
        using var router = new RouterSocket(ctx);

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

        using var ctx = new Context();
        using var dealer = new DealerSocket(ctx);
        using var router = new RouterSocket(ctx);
        using var stream = new StreamSocket(ctx);
        using var xpub = new XPubSocket(ctx);

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

        using var ctx = new Context();
        using var dealer = new DealerSocket(ctx);
        using var router = new RouterSocket(ctx);

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

        using var ctx = new Context();
        using var sub = new SubSocket(ctx);
        using var node = new SpotNode(ctx);
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

        using var ctx = new Context();
        using var node = new SpotNode(ctx);

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

        using var ctx = new Context();
        using var node = new SpotNode(ctx);

        Assert.Null(node.SpotLookup(CoreTestSupport.RoutingIdUtf8("missing")));
    }

    [Fact]
    public void socket_options_reject_incompatible_socket_types()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var pub = new PubSocket(ctx);
        using var sub = new SubSocket(ctx);
        using var dealer = new DealerSocket(ctx);

        Assert.Throws<InvalidOperationException>(() =>
            pub.SetOption(SocketOptions.Subscribe, string.Empty));
        Assert.Throws<InvalidOperationException>(() =>
            sub.SetOption(SocketOptions.XPubVerbose, 1));
        Assert.Throws<InvalidOperationException>(() =>
            dealer.SetOption(SocketOptions.StreamNotify, 1));
    }
}
