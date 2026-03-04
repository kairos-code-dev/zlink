using System;
using System.Collections.Generic;
using System.Linq;
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
    public void socket_option_aliases_share_native_ids()
    {
        Assert.Equal(SocketOptions.RoutingId.Option,
            SocketOptions.RoutingIdBytes.Option);
        Assert.Equal(SocketOptions.ConnectRoutingId.Option,
            SocketOptions.ConnectRoutingIdBytes.Option);
    }

    [Fact]
    public void socket_options_runtime_int_and_long_roundtrip()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var stream = new Socket(ctx, SocketType.Stream);
        using var dealer = new Socket(ctx, SocketType.Dealer);

        stream.SetOption(SocketOptions.StreamNotify, 0);
        Assert.Equal(0, stream.GetOption(SocketOptions.StreamNotify));

        stream.SetOption(SocketOptions.MaxMsgSize, 1024L);
        Assert.Equal(1024L, stream.GetOption(SocketOptions.MaxMsgSize));

        dealer.SetOption(SocketOptions.RoutingIdBytes, "RID-OPT"u8.ToArray());
        byte[] rid = dealer.GetOption(SocketOptions.RoutingIdBytes);
        Assert.True(rid.Length >= 7);
        Assert.Equal("RID-OPT"u8.ToArray(), rid.Take(7).ToArray());
    }

    [Fact]
    public void socket_options_runtime_string_getter_works()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var router = new Socket(ctx, SocketType.Router);

        string endpoint = CoreTestSupport.NewEndpoint("tcp",
            "socket-options-last-endpoint");
        router.Bind(endpoint);

        string actual = router.GetOption(SocketOptions.LastEndpoint);
        Assert.StartsWith("tcp://", actual);
    }
}
