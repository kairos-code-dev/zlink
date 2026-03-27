using System;
using System.Linq;
using System.Reflection;
using Xunit;

namespace Zlink.Tests;

public sealed class test_socket_surface
{
    private static bool HasPublicInstanceMethod(Type type, string name)
    {
        return type.GetMethods(BindingFlags.Instance | BindingFlags.Public)
            .Any(method => method.Name == name);
    }

    [Fact]
    public void typed_surface_hides_irrelevant_methods()
    {
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket), nameof(Socket.Receive)));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket), nameof(Socket.Subscribe)));
        Assert.False(HasPublicInstanceMethod(typeof(SubSocket), nameof(Socket.Publish)));

        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(StreamSocket.AttachStreamRaw)));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket),
            nameof(StreamSocket.AttachStreamRaw)));
        Assert.True(HasPublicInstanceMethod(typeof(XPubSocket),
            nameof(XPubSocket.ReceiveSubscriptionEvent)));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket),
            nameof(XPubSocket.ReceiveSubscriptionEvent)));
    }

    [Fact]
    public void compat_socket_keeps_legacy_union_surface()
    {
        Assert.True(HasPublicInstanceMethod(typeof(Socket), nameof(Socket.Send)));
        Assert.True(HasPublicInstanceMethod(typeof(Socket), nameof(Socket.Receive)));
        Assert.True(HasPublicInstanceMethod(typeof(Socket), nameof(Socket.Publish)));
        Assert.True(HasPublicInstanceMethod(typeof(Socket), nameof(Socket.Subscribe)));
        Assert.True(HasPublicInstanceMethod(typeof(Socket),
            nameof(Socket.ReceiveSubscriptionEvent)));
        Assert.True(HasPublicInstanceMethod(typeof(Socket),
            nameof(Socket.AttachStreamRaw)));
    }

    [Fact]
    public void compat_socket_roundtrips_with_typed_socket()
    {
        if (!CoreTestSupport.IsNativeAvailable())
            return;

        using var ctx = new Context();
        using var compat = new Socket(ctx, SocketType.Pair);
        using var typed = new PairSocket(ctx);
        string endpoint = CoreTestSupport.NewEndpoint("tcp", "compat-typed-pair");
        compat.Bind(endpoint);
        typed.Connect(endpoint);

        CoreTestSupport.SendWithRetry(compat, "compat-surface"u8, SendFlags.None,
            3000);

        using Message received = CoreTestSupport.ReceiveMessageWithTimeout(typed,
            3000);
        Assert.Equal("compat-surface", CoreTestSupport.Utf8(received));
    }
}
