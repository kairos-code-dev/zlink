using System;
using System.Collections.Generic;
using System.Reflection;
using Xunit;

namespace Zlink.Tests;

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

    private static readonly HashSet<string> StringOptions = new()
    {
        nameof(SocketOptions.RoutingId),
        nameof(SocketOptions.Subscribe),
        nameof(SocketOptions.Unsubscribe),
        nameof(SocketOptions.LastEndpoint),
        nameof(SocketOptions.ConnectRoutingId),
        nameof(SocketOptions.XPubWelcomeMsg),
        nameof(SocketOptions.BindToDevice),
        nameof(SocketOptions.TlsCert),
        nameof(SocketOptions.TlsKey),
        nameof(SocketOptions.TlsCa),
        nameof(SocketOptions.TlsHostname),
        nameof(SocketOptions.TlsPassword),
        nameof(SocketOptions.ZmpMetadata)
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

        stream.SetNotify(false);
        Assert.False(stream.GetNotify());

        stream.SetOption(SocketOptions.MaxMsgSize, 1024L);
        Assert.Equal(1024L, stream.GetOption(SocketOptions.MaxMsgSize));

        dealer.SetRoutingId("RID-OPT");
        Assert.Equal("RID-OPT", dealer.GetRoutingId());
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

        string actual = router.GetOption(SocketOptions.LastEndpoint);
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

        dealer.SetRoutingId("DEALER-RID");
        Assert.Equal("DEALER-RID", dealer.GetRoutingId());

        router.SetRoutingId("ROUTER-RID");
        Assert.Equal("ROUTER-RID", router.GetRoutingId());
        router.SetMandatory(true);
        Assert.True(router.GetMandatory());

        stream.SetNotify(true);
        Assert.True(stream.GetNotify());

        xpub.SetVerbose(true);
        xpub.SetVerboser(true);
        xpub.SetNoDrop(true);
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
