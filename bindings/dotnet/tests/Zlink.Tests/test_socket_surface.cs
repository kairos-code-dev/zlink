using System;
using System.ComponentModel;
using System.Linq;
using System.Reflection;
using Xunit;
using Zlink.Service;

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
        Assert.False(HasPublicInstanceMethod(typeof(SocketBase),
            "AttachDiscovery"));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket), "Recv"));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket), "Subscribe"));
        Assert.False(HasPublicInstanceMethod(typeof(SubSocket), "Publish"));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket), "Send",
            typeof(string), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(DealerSocket), "Send",
            typeof(string), typeof(Message)));
        Assert.True(HasPublicInstanceMethod(typeof(PairSocket), "Recv",
            Type.EmptyTypes));
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            "Recv", Type.EmptyTypes));
        Assert.True(HasPublicInstanceMethod(typeof(PairSocket),
            nameof(MessageSocketBase.OnSendReady)));
        Assert.True(HasPublicInstanceMethod(typeof(PubSocket),
            nameof(PublisherSocketBase.OnSendReady)));
        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(RoutedMessageSocketBase.OnSendReady)));
        Assert.False(HasPublicInstanceMethod(typeof(SubSocket), "OnSendReady"));
        Assert.False(HasPublicInstanceMethod(typeof(XSubSocket), "OnSendReady"));
        Assert.False(HasPublicInstanceMethod(typeof(RouterSocket), "Send",
            typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket), "Send",
            typeof(Message)));
        Assert.True(HasPublicInstanceMethod(typeof(RouterSocket), "Recv",
            Type.EmptyTypes));
        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket), "Recv",
            Type.EmptyTypes));

        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(StreamSocket.AttachStreamRaw)));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket), "Connect"));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket), "Disconnect"));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket),
            nameof(StreamSocket.AttachStreamRaw)));
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            nameof(DealerSocket.AttachDiscovery)));
        Assert.True(HasPublicInstanceMethod(typeof(RouterSocket),
            nameof(RouterSocket.AttachDiscovery)));
        Assert.True(HasPublicInstanceMethod(typeof(PubSocket),
            nameof(PubSocket.AttachDiscovery)));
        Assert.True(HasPublicInstanceMethod(typeof(SubSocket),
            nameof(SubSocket.AttachDiscovery)));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket),
            nameof(DealerSocket.AttachDiscovery)));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(DealerSocket.AttachDiscovery)));
        Assert.False(HasPublicInstanceMethod(typeof(XPubSocket),
            nameof(DealerSocket.AttachDiscovery)));
        Assert.False(HasPublicInstanceMethod(typeof(XSubSocket),
            nameof(DealerSocket.AttachDiscovery)));
        Assert.True(HasPublicInstanceMethod(typeof(PairSocket),
            nameof(MessageSocketBase.OnReceive)));
        Assert.True(HasPublicInstanceMethod(typeof(XPubSocket),
            nameof(XPubSocket.ReceiveSubscriptionEvent)));
        Assert.True(HasPublicInstanceMethod(typeof(XPubSocket),
            nameof(XPubSocket.TryReceiveSubscriptionEvent),
            typeof(SubscriptionEvent).MakeByRefType()));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket),
            nameof(XPubSocket.ReceiveSubscriptionEvent)));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket),
            nameof(XPubSocket.TryReceiveSubscriptionEvent),
            typeof(SubscriptionEvent).MakeByRefType()));
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            nameof(MessageSocketBase.OnReceive)));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket),
            nameof(SubscriberSocketBase.OnSubscribe)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Context)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(SocketBase)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(SocketMonitor)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(ServiceMonitor)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Message)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Poller)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(AtomicCounter)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(ZlinkStopwatch)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Registry)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Discovery)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(RegistryQueryClient)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(SpotNode)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Spot)));
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

        Assert.False(HasPublicInstanceMethod(typeof(Context), "SetOption"));
        Assert.False(HasPublicInstanceMethod(typeof(Context), "GetOption"));
        Assert.Null(typeof(ContextOptions).GetProperty("ThreadNamePrefix",
            BindingFlags.Instance | BindingFlags.Public));

        Assert.Null(typeof(MessageSocketBase).GetMethod("Recv",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: new[] { typeof(int) }, modifiers: null));
        Assert.Null(typeof(MessageSocketBase).GetMethod("TryRecv",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: Type.EmptyTypes, modifiers: null));
        Assert.Null(typeof(PublisherSocketBase).GetMethod("Publish",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: new[] { typeof(string), typeof(Message), typeof(int) },
            modifiers: null));
        Assert.Null(typeof(SocketMonitor).GetMethod("Recv",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: new[] { typeof(int) }, modifiers: null));
        Assert.Null(typeof(SocketMonitor).GetMethod("TryRecv",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: Type.EmptyTypes, modifiers: null));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "SetOption"));
        Assert.False(HasPublicInstanceMethod(typeof(Message),
            "GetPropertyString"));
        Assert.True(typeof(SocketBase).GetMethod(nameof(SocketBase.MonitorOpen))!
            .GetParameters()[0].HasDefaultValue);
        Assert.Equal(SocketEvent.All,
            typeof(SocketBase).GetMethod(nameof(SocketBase.MonitorOpen))!
                .GetParameters()[0].DefaultValue);
        Assert.True(typeof(Discovery).GetMethod(nameof(Discovery.MonitorOpen))!
            .GetParameters()[0].HasDefaultValue);
        Assert.Equal(ServiceMonitorEvents.All,
            typeof(Discovery).GetMethod(nameof(Discovery.MonitorOpen))!
                .GetParameters()[0].DefaultValue);
    }

    [Fact]
    public void monitor_surface_exposes_canonical_receive_methods_only()
    {
        Assert.True(HasPublicInstanceMethod(typeof(SocketMonitor), "Recv"));
        Assert.True(HasPublicInstanceMethod(typeof(SocketMonitor), "TryRecv",
            typeof(SocketMonitorEvent?).MakeByRefType()));
        Assert.True(HasPublicInstanceMethod(typeof(SocketMonitor),
            "OnEvent"));
        Assert.False(HasPublicInstanceMethod(typeof(SocketMonitor), "Receive"));
        Assert.False(HasPublicInstanceMethod(typeof(SocketMonitor), "TryReceive"));
    }

    [Fact]
    public void raw_option_types_are_not_publicly_exported()
    {
        Type[] exported = typeof(SocketBase).Assembly.GetExportedTypes();

        Assert.DoesNotContain(exported, type => type.Name == "Socket");
        Assert.DoesNotContain(exported, type => type.Name == nameof(SocketOptions));
        Assert.DoesNotContain(exported,
            type => type.Name == typeof(SocketOptionKey<int>).Name);
        Assert.DoesNotContain(exported, type => type.Name == nameof(SocketOption));
        Assert.DoesNotContain(exported, type => type.Name == nameof(ContextOption));
    }

    [Fact]
    public void typed_option_facades_and_service_monitor_events_are_public()
    {
        Assert.Equal(typeof(CommonSocketOptions),
            typeof(SocketBase).GetProperty(nameof(SocketBase.Options))!.PropertyType);
        Assert.Equal(typeof(ContextOptions),
            typeof(Context).GetProperty(nameof(Context.Options))!.PropertyType);
        Assert.Equal(typeof(RouterSocketOptions),
            typeof(RouterSocket).GetProperty(nameof(RouterSocket.RouterOptions))!
                .PropertyType);
        Assert.Equal(typeof(DealerSocketOptions),
            typeof(DealerSocket).GetProperty(nameof(DealerSocket.DealerOptions))!
                .PropertyType);
        Assert.Equal(typeof(StreamSocketOptions),
            typeof(StreamSocket).GetProperty(nameof(StreamSocket.StreamOptions))!
                .PropertyType);
        Assert.Equal(typeof(SubSocketOptions),
            typeof(SubSocket).GetProperty(nameof(SubSocket.SubOptions))!
                .PropertyType);
        Assert.Equal(typeof(ServiceMonitorEvents),
            typeof(ServiceMonitorEvent).GetProperty(nameof(ServiceMonitorEvent.EventType))!
                .PropertyType);
        Assert.False(HasPublicInstanceMethod(typeof(Spot), "MonitorOpen",
            typeof(ServiceMonitorEvents)));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "MonitorOpen",
            typeof(ServiceMonitorEvents)));
    }

    [Fact]
    public void service_surface_uses_canonical_names_and_types()
    {
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "StatusSnapshot"));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "PeersSnapshot"));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "PeersQuery",
            typeof(SpotNodePeerFilter)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "SubjectsSnapshot",
            typeof(SpotNodeSubjectFilter?)));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "MonitorOpen",
            typeof(ServiceMonitorEvents)));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "Snapshot"));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "Peers"));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "Subjects"));

        Assert.True(HasPublicInstanceMethod(typeof(Discovery),
            "MemberPeerMetadata", typeof(ServiceRole), typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(Discovery), "MonitorOpen",
            typeof(ServiceMonitorEvents)));
        Assert.False(HasPublicInstanceMethod(typeof(Discovery),
            "GetMemberPeerMetadata"));

        Assert.True(HasPublicInstanceMethod(typeof(ServiceMonitor), "Recv"));
        Assert.True(HasPublicInstanceMethod(typeof(ServiceMonitor), "TryRecv",
            typeof(ServiceMonitorEvent?).MakeByRefType()));
        Assert.True(HasPublicInstanceMethod(typeof(ServiceMonitor), "OnEvent",
            typeof(Action<ServiceMonitorEvent>)));
        Assert.False(HasPublicInstanceMethod(typeof(ServiceMonitor), "Receive"));
        Assert.False(HasPublicInstanceMethod(typeof(ServiceMonitor), "TryReceive"));
        Assert.False(HasPublicInstanceMethod(typeof(ServiceMonitor),
            "AttachHandler"));
        Assert.True(HasPublicInstanceMethod(typeof(Message), "GetProperty",
            typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(SocketBase), "MonitorOpen",
            typeof(SocketEvent)));
        Assert.True(HasPublicInstanceMethod(typeof(Discovery), "MonitorOpen",
            typeof(ServiceMonitorEvents)));
    }

    [Fact]
    public void generic_socket_management_surface_uses_public_interface()
    {
        Assert.Equal(typeof(IZlinkSocket),
            typeof(Runtime).GetMethod(nameof(Runtime.Proxy))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(Runtime).GetMethod(nameof(Runtime.ProxySteerable))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(Poller).GetMethod(nameof(Poller.Add),
                new[] { typeof(IZlinkSocket), typeof(PollEvents), typeof(object) })!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(Poller).GetMethod(nameof(Poller.Modify))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(Poller).GetMethod(nameof(Poller.Remove),
                new[] { typeof(IZlinkSocket) })!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(PollEvent).GetProperty(nameof(PollEvent.Socket))!.PropertyType);
        Assert.NotNull(typeof(ZlinkPoll).GetMethod(nameof(ZlinkPoll.Poll),
            new[]
            {
                typeof(IReadOnlyList<IZlinkSocket>),
                typeof(IReadOnlyList<PollEvents>),
                typeof(Span<PollEvents>),
                typeof(int)
            }));
    }

    [Fact]
    public void abstract_socket_base_types_are_hidden_from_intellisense()
    {
        Type[] hiddenTypes =
        {
            typeof(SocketBase),
            typeof(ConnectableSocketBase),
            typeof(MessageSocketBase),
            typeof(PublisherSocketBase),
            typeof(RoutedMessageSocketBase),
            typeof(ConnectableRoutedMessageSocketBase),
            typeof(SubscriberSocketBase)
        };

        foreach (Type hiddenType in hiddenTypes)
        {
            EditorBrowsableAttribute? attribute =
                hiddenType.GetCustomAttribute<EditorBrowsableAttribute>();
            Assert.NotNull(attribute);
            Assert.Equal(EditorBrowsableState.Never, attribute!.State);
        }
    }
}
