using System;
using System.Collections.Generic;
using System.ComponentModel;
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
        return PublicInstanceMethods(type).Any(method =>
            method.Name == name
            && method.GetParameters().Select(p => p.ParameterType)
                .SequenceEqual(parameterTypes));
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
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "CreateSpot"));
        Assert.True(HasPublicInstanceMethod(typeof(PairSocket), "Recv",
            typeof(RecvFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            "Recv", typeof(RecvFlags)));
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
            typeof(RecvFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket), "Recv",
            typeof(RecvFlags)));

        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(StreamSocket.OnPacket)));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket), "Connect"));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket), "Disconnect"));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket),
            nameof(StreamSocket.OnPacket)));
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
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket),
            nameof(MessageSocketBase.OnReceive)));
        Assert.False(HasPublicInstanceMethod(typeof(DealerSocket),
            nameof(MessageSocketBase.OnReceive)));
        Assert.False(HasPublicInstanceMethod(typeof(RouterSocket),
            nameof(RoutedMessageSocketBase.OnReceive)));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(RoutedMessageSocketBase.OnReceive)));
        Assert.True(HasPublicInstanceMethod(typeof(XPubSocket),
            nameof(XPubSocket.ReceiveSubscriptionEvent)));
        Assert.False(HasPublicInstanceMethod(typeof(XPubSocket),
            "ReceiveSubscriptionEventNoWait",
            typeof(SubscriptionEvent).MakeByRefType()));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket),
            nameof(XPubSocket.ReceiveSubscriptionEvent)));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket),
            "ReceiveSubscriptionEventNoWait",
            typeof(SubscriptionEvent).MakeByRefType()));
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket),
            "OnSubscribe"));
        Assert.False(HasPublicInstanceMethod(typeof(SubSocket),
            "OnSubscribe"));
        Assert.False(HasPublicInstanceMethod(typeof(XSubSocket),
            "OnSubscribe"));
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
        Assert.NotNull(typeof(SpotNode).GetMethod(nameof(SpotNode.CreateSpot),
            BindingFlags.Instance | BindingFlags.Public));
        Assert.Empty(typeof(Spot).GetConstructors(
            BindingFlags.Instance | BindingFlags.Public));
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

        Assert.True(HasPublicInstanceMethod(typeof(MessageSocketBase), "Send",
            typeof(Message), typeof(SendFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(MessageSocketBase), "Send",
            typeof(IReadOnlyList<Message>), typeof(SendFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(MessageSocketBase), "Recv",
            typeof(RecvFlags)));
        Assert.False(HasPublicInstanceMethod(typeof(MessageSocketBase), "TrySend",
            typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(MessageSocketBase), "TrySend",
            typeof(IReadOnlyList<Message>)));
        Assert.False(HasPublicInstanceMethod(typeof(MessageSocketBase), "TryRecv",
            typeof(Received).MakeByRefType()));
        Assert.True(HasPublicInstanceMethod(typeof(PublisherSocketBase), "Publish",
            typeof(string), typeof(Message), typeof(SendFlags)));
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
        MethodInfo discoveryMonitorOpen = typeof(Discovery).GetMethods(
            BindingFlags.Instance | BindingFlags.Public)
            .Single(method =>
                method.Name == nameof(Discovery.MonitorOpen)
                && method.GetParameters().Length == 1
                && method.GetParameters()[0].ParameterType
                    == typeof(ServiceMonitorEventMask[]));
        Assert.True(discoveryMonitorOpen.GetParameters()[0]
            .GetCustomAttribute<ParamArrayAttribute>() != null);
    }

    [Fact]
    public void monitor_surface_exposes_canonical_receive_methods_only()
    {
        Assert.True(HasPublicInstanceMethod(typeof(SocketMonitor), "Recv"));
        Assert.True(HasPublicInstanceMethod(typeof(SocketMonitor), "Recv",
            typeof(bool)));
        Assert.False(HasPublicInstanceMethod(typeof(SocketMonitor), "TryRecv",
            typeof(SocketMonitorEvent).MakeByRefType()));
        Assert.True(HasPublicInstanceMethod(typeof(SocketMonitor),
            "OnEvent"));
        Assert.False(HasPublicInstanceMethod(typeof(SocketMonitor), "Receive"));
        Assert.False(HasPublicInstanceMethod(typeof(SocketMonitor), "TryReceive"));
    }

    [Fact]
    public void request_reply_surface_uses_canonical_socket_methods()
    {
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            nameof(DealerSocket.RequestAsync), typeof(Message),
            typeof(System.Threading.CancellationToken)));
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            nameof(DealerSocket.Request), typeof(Message),
            typeof(Action<RequestResult, IReadOnlyList<Message>>),
            typeof(SendFlags), typeof(TimeSpan?)));
        Assert.False(HasPublicInstanceMethod(typeof(DealerSocket),
            nameof(DealerSocket.OnReceive), typeof(SocketRecvHandler)));
        Assert.True(HasPublicInstanceMethod(typeof(RouterSocket),
            nameof(RouterSocket.RequestAsync), typeof(RoutingId),
            typeof(Message), typeof(System.Threading.CancellationToken)));
        Assert.True(HasPublicInstanceMethod(typeof(RouterSocket),
            nameof(RouterSocket.Request), typeof(RoutingId), typeof(Message),
            typeof(Action<RequestResult, IReadOnlyList<Message>>),
            typeof(SendFlags), typeof(TimeSpan?)));
        Assert.True(HasPublicInstanceMethod(typeof(RouterSocket),
            nameof(RouterSocket.Reply), typeof(RoutingId), typeof(ulong),
            typeof(Message), typeof(SendFlags)));
        Assert.False(HasPublicInstanceMethod(typeof(RouterSocket), "RecvSpot"));
        Assert.False(HasPublicInstanceMethod(typeof(RouterSocket), "OnSpotReceive"));
        Assert.True(HasPublicInstanceMethod(typeof(RoutedMessageSocketBase), "Send",
            typeof(RoutingId), typeof(Message), typeof(SendFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(RoutedMessageSocketBase), "Send",
            typeof(string), typeof(Message), typeof(SendFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(RoutedMessageSocketBase), "Recv",
            typeof(RecvFlags)));
        Assert.False(HasPublicInstanceMethod(typeof(RoutedMessageSocketBase), "TrySend",
            typeof(RoutingId), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(RoutedMessageSocketBase), "TrySend",
            typeof(string), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(RoutedMessageSocketBase), "TryRecv",
            typeof(Received).MakeByRefType()));
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
        Assert.Equal(typeof(ServiceEventType),
            typeof(ServiceMonitorEvent).GetProperty(nameof(ServiceMonitorEvent.EventType))!
                .PropertyType);
        Assert.False(HasPublicInstanceMethod(typeof(Spot), "MonitorOpen",
            typeof(ServiceMonitorEvents)));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "MonitorOpen",
            typeof(ServiceMonitorEvents)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "SetAdmissionState",
            typeof(AdmissionState)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "GetAdmissionState"));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "AttachChannelDealer",
            typeof(Discovery), typeof(DealerSocket)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "AttachChannelDealerManual",
            typeof(string), typeof(DealerSocket)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "AttachPubIngress",
            typeof(PubSocket)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.SetAdmissionState), typeof(AdmissionState)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.GetAdmissionState)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot), nameof(Spot.Publish),
            typeof(string), typeof(string), typeof(Message),
            typeof(SendFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot), nameof(Spot.SendChannel),
            typeof(string), typeof(Message), typeof(SendFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.RequestChannelAsync), typeof(string), typeof(Message),
            typeof(TimeSpan), typeof(System.Threading.CancellationToken)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.ReceiveSubscriptionEvent), typeof(RecvFlags)));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket), "TryPublish",
            typeof(string), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket), "TryPublish",
            typeof(string), typeof(IReadOnlyList<Message>)));
        Assert.False(HasPublicInstanceMethod(typeof(SubSocket), "TrySubscribe"));
    }

    [Fact]
    public void service_surface_uses_canonical_names_and_types()
    {
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "StatusSnapshot"));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "PeersSnapshot"));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "PeersQuery",
            typeof(SpotNodePeerFilter)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "SubjectsSnapshot",
            typeof(SpotNodeSubjectFilter)));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "MonitorOpen",
            typeof(ServiceMonitorEvents)));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "Snapshot"));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "Peers"));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "Subjects"));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode),
            nameof(SpotNode.SetRoutingId), typeof(RoutingId)));
        Assert.Equal(typeof(RoutingId),
            typeof(SpotNode).GetProperty(nameof(SpotNode.RoutingId))!.PropertyType);

        Assert.True(HasPublicInstanceMethod(typeof(Discovery),
            "MemberPeerMetadata", typeof(ServiceRole), typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(Discovery),
            nameof(Discovery.ResolveSpot), typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(Discovery),
            nameof(Discovery.SetDealerPeerMode), typeof(DiscoveryDealerPeerMode)));
        Assert.True(HasPublicInstanceMethod(typeof(Discovery), "MonitorOpen",
            typeof(ServiceMonitorEventMask[])));
        Assert.False(HasPublicInstanceMethod(typeof(Discovery), "MonitorOpen",
            typeof(ServiceMonitorEvents)));
        Assert.False(HasPublicInstanceMethod(typeof(Discovery),
            "GetMemberPeerMetadata"));
        Assert.True(typeof(DiscoveryDealerPeerMode).IsEnum);

        Assert.True(HasPublicInstanceMethod(typeof(ServiceMonitor), "Recv"));
        Assert.True(HasPublicInstanceMethod(typeof(ServiceMonitor), "Recv",
            typeof(bool)));
        Assert.False(HasPublicInstanceMethod(typeof(ServiceMonitor), "TryRecv",
            typeof(ServiceMonitorEvent).MakeByRefType()));
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
        Assert.False(HasPublicInstanceMethod(typeof(Discovery), "MonitorOpen",
            typeof(ServiceMonitorEvents)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.SetRoutingId), typeof(RoutingId)));
        Assert.Equal(typeof(RoutingId),
            typeof(Spot).GetProperty(nameof(Spot.RoutingId))!.PropertyType);
    }

    [Fact]
    public void generic_socket_management_surface_uses_public_interface()
    {
        Assert.Equal(typeof(IZlinkSocket),
            typeof(global::Zlink.Zlink).GetMethod(nameof(global::Zlink.Zlink.Proxy))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(global::Zlink.Zlink).GetMethod(
                nameof(global::Zlink.Zlink.ProxySteerable))!
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
        Assert.Equal(typeof(int),
            typeof(Poller).GetProperty(nameof(Poller.Size))!.PropertyType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(PollEvent).GetProperty(nameof(PollEvent.Socket))!.PropertyType);
        Assert.NotNull(typeof(ZlinkPoll).GetMethod(nameof(ZlinkPoll.Poll),
            new[]
            {
                typeof(IReadOnlyList<IZlinkSocket>),
                typeof(int)
            }));
        Assert.Equal(typeof(Action<MonitorEvent>),
            typeof(SocketMonitor).GetField(nameof(SocketMonitor.IgnoreHandler),
                BindingFlags.Static | BindingFlags.Public)!.FieldType);
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
