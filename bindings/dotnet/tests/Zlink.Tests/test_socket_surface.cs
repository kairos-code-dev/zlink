using System;
using System.Linq;
using System.Reflection;
using Xunit;

namespace Zlink.Tests;

public sealed class test_socket_surface
{
    private static MethodInfo[] PublicInstanceMethods(Type type)
    {
        return type.GetMethods(BindingFlags.Instance | BindingFlags.Public);
    }

    private static bool HasPublicInstanceMethod(Type type, string name)
    {
        return PublicInstanceMethods(type).Any(method => method.Name == name);
    }

    private static bool HasPublicInstanceMethod(Type type, string name,
        params Type[] parameterTypes)
    {
        return type.GetMethod(name, BindingFlags.Instance | BindingFlags.Public,
            binder: null, types: parameterTypes, modifiers: null) != null;
    }

    [Fact]
    public void typed_surface_hides_irrelevant_methods()
    {
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket), "SetOption"));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket), "GetOption"));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket), "Receive"));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket), "Subscribe"));
        Assert.False(HasPublicInstanceMethod(typeof(SubSocket), "Publish"));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket), "Send",
            typeof(string), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(DealerSocket), "Send",
            typeof(string), typeof(Message)));
        Assert.True(HasPublicInstanceMethod(typeof(PairSocket), "Receive",
            Type.EmptyTypes));
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            "Receive", Type.EmptyTypes));
        Assert.False(HasPublicInstanceMethod(typeof(RouterSocket), "Send",
            typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket), "Send",
            typeof(Message)));
        Assert.True(HasPublicInstanceMethod(typeof(RouterSocket), "Receive",
            Type.EmptyTypes));
        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket), "Receive",
            Type.EmptyTypes));

        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(StreamSocket.AttachStreamRaw)));
        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(StreamSocket.SetNotify)));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket),
            nameof(StreamSocket.AttachStreamRaw)));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket),
            nameof(StreamSocket.SetNotify)));
        Assert.True(HasPublicInstanceMethod(typeof(XPubSocket),
            nameof(XPubSocket.ReceiveSubscriptionEvent)));
        Assert.True(HasPublicInstanceMethod(typeof(XPubSocket),
            nameof(XPubSocket.SetVerbose)));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket),
            nameof(XPubSocket.ReceiveSubscriptionEvent)));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket),
            nameof(XPubSocket.SetVerbose)));
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            nameof(DealerSocket.SetRoutingId)));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket),
            nameof(DealerSocket.SetRoutingId)));
    }

    [Fact]
    public void public_surface_removes_raw_flags_and_option_bags()
    {
        Type[] socketTypes =
        {
            typeof(SocketBase),
            typeof(MessageSocketBase),
            typeof(PublisherSocketBase),
            typeof(SubscriberSocketBase),
            typeof(PairSocket),
            typeof(PubSocket),
            typeof(SubSocket),
            typeof(DealerSocket),
            typeof(RouterSocket),
            typeof(StreamSocket),
            typeof(XPubSocket),
            typeof(XSubSocket)
        };

        foreach (Type socketType in socketTypes)
        {
            Assert.False(HasPublicInstanceMethod(socketType, "SetOption"));
            Assert.False(HasPublicInstanceMethod(socketType, "GetOption"));
        }

        Assert.Null(typeof(MessageSocketBase).GetMethod("Receive",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: new[] { typeof(int) }, modifiers: null));
        Assert.Null(typeof(MessageSocketBase).GetMethod("TryReceive",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: new[] { typeof(int) }, modifiers: null));
        Assert.Null(typeof(PublisherSocketBase).GetMethod("Publish",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: new[] { typeof(string), typeof(Message), typeof(int) },
            modifiers: null));
        Assert.Null(typeof(SocketMonitor).GetMethod("Receive",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: new[] { typeof(int) }, modifiers: null));
        Assert.Null(typeof(SocketMonitor).GetMethod("TryReceive",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: new[] { typeof(int) }, modifiers: null));
    }

    [Fact]
    public void monitor_surface_exposes_canonical_receive_methods_only()
    {
        Assert.True(HasPublicInstanceMethod(typeof(SocketMonitor), "Receive"));
        Assert.True(HasPublicInstanceMethod(typeof(SocketMonitor), "TryReceive"));
        Assert.True(HasPublicInstanceMethod(typeof(SocketMonitor),
            "AttachHandler"));
        Assert.False(HasPublicInstanceMethod(typeof(SocketMonitor), "Recv"));
        Assert.False(HasPublicInstanceMethod(typeof(SocketMonitor), "TryRecv"));
    }
}
