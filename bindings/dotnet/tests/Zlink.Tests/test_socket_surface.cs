using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Reflection;
using Xunit;

namespace Systems.Zlink.Tests;

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
            typeof(Received), typeof(RecvFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            "Recv", typeof(Received), typeof(RecvFlags)));
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
            typeof(Received), typeof(RecvFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket), "Recv",
            typeof(Received), typeof(RecvFlags)));

        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(StreamSocket.OnPacket)));
        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(StreamSocket.SetRoutingId), typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(StreamSocket.GetRoutingId)));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket),
            "OnFramedPacket"));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket), "Send",
            typeof(uint), typeof(Message), typeof(SendFlags)));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket), "Send",
            typeof(uint), typeof(byte[]), typeof(SendFlags)));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket), "Connect"));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket), "Disconnect"));
        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket), "DisconnectRid",
            typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(PairSocket), "DisconnectRid",
            typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(RouterSocket), "DisconnectRid",
            typeof(RoutingId)));
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
        Assert.False(HasPublicInstanceMethod(typeof(PairSocket), "OnReceive"));
        Assert.False(HasPublicInstanceMethod(typeof(DealerSocket), "OnReceive"));
        Assert.False(HasPublicInstanceMethod(typeof(RouterSocket), "OnReceive"));
        Assert.False(HasPublicInstanceMethod(typeof(StreamSocket), "OnReceive"));
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
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Message)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Poller)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(AtomicCounter)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(ZlinkStopwatch)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Registry)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Discovery)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(RegistryQueryClient)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(SpotNode)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Spot)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Actor)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "DisconnectPeerRid",
            typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode),
            nameof(SpotNode.CreateActor), typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode),
            nameof(SpotNode.ActorLookup), typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode),
            nameof(SpotNode.GetOrCreateSpot),
            typeof(RoutingId),
            typeof(bool).MakeByRefType()));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode),
            nameof(SpotNode.RemoteActorGetRef), typeof(RoutingId),
            typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode),
            nameof(SpotNode.JoinActor), typeof(ActorRef), typeof(RoutingId),
            typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.RecvActorJoin), typeof(RecvFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.ReplyActorJoin), typeof(ActorJoinRequest), typeof(bool)));
        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(StreamSocket.BindActor), typeof(RoutingId),
            typeof(ActorRef)));
        Assert.True(HasPublicInstanceMethod(typeof(StreamSocket),
            nameof(StreamSocket.SendBoundActor), typeof(RoutingId),
            typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(Discovery),
            nameof(Discovery.ResolveActor), typeof(string)));
        Assert.True(typeof(ActorRef).GetProperty(nameof(ActorRef.IsUnchecked))
            is not null);
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
        Assert.NotNull(typeof(ContextOptions).GetProperty("ThreadNamePrefix",
            BindingFlags.Instance | BindingFlags.Public));
        Assert.NotNull(typeof(ContextOptions).GetProperty(
            nameof(ContextOptions.AutoHwmMessageUnitBytes),
            BindingFlags.Instance | BindingFlags.Public));
        Assert.Null(typeof(CommonSocketOptions).GetProperty(
            "AutoHwmMessageUnitBytes",
            BindingFlags.Instance | BindingFlags.Public));

        Assert.True(HasPublicInstanceMethod(typeof(MessageSocketBase), "Send"));
        Assert.False(HasPublicInstanceMethod(typeof(MessageSocketBase), "Send",
            typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(MessageSocketBase), "Send",
            typeof(IReadOnlyList<Message>)));
        Assert.True(HasPublicInstanceMethod(typeof(MessageSocketBase), "Recv",
            typeof(Received), typeof(RecvFlags)));
        Assert.False(HasPublicInstanceMethod(typeof(MessageSocketBase), "TrySend",
            typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(MessageSocketBase), "TrySend",
            typeof(IReadOnlyList<Message>)));
        Assert.False(HasPublicInstanceMethod(typeof(MessageSocketBase), "TryRecv",
            typeof(Received).MakeByRefType()));
        Assert.True(HasPublicInstanceMethod(typeof(PublisherSocketBase), "Publish",
            typeof(string)));
        Assert.False(HasPublicInstanceMethod(typeof(PublisherSocketBase),
            "Publish", typeof(string), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(PublisherSocketBase),
            "Publish", typeof(string), typeof(IReadOnlyList<Message>)));
        Assert.False(HasPublicInstanceMethod(typeof(DealerSocket), "Request",
            typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(RouterSocket), "Reply",
            typeof(RoutingId), typeof(ulong), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(Spot), "Publish",
            typeof(string), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(Spot), "SendChannel",
            typeof(string), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(Spot), "RequestChannel",
            typeof(string), typeof(Message)));
        Assert.NotNull(typeof(Received).GetMethod(nameof(Received.Reply),
            Type.EmptyTypes));
        Assert.Null(typeof(Received).GetMethod(nameof(Received.Reply),
            new[] { typeof(Message) }));
        Assert.Null(typeof(Received).GetMethod(nameof(Received.Reply),
            new[] { typeof(IReadOnlyList<Message>) }));
        Assert.Null(typeof(SocketMonitor).GetMethod("Recv",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: new[] { typeof(int) }, modifiers: null));
        Assert.Null(typeof(SocketMonitor).GetMethod("TryRecv",
            BindingFlags.Instance | BindingFlags.Public, binder: null,
            types: Type.EmptyTypes, modifiers: null));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "SetOption"));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode),
            "SetActorHighWaterMark"));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "DestroyActor",
            typeof(ActorRef)));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode),
            "SetRouterHighWaterMark"));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode),
            "SetPubSubHighWaterMark"));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode),
            "SetRouterHighWaterMarkProfile"));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode),
            "SetPubSubHighWaterMarkProfile"));
        Assert.Null(typeof(SpotNode).GetProperty("AutoHwmMessageUnitBytes",
            BindingFlags.Instance | BindingFlags.Public));
        Assert.Null(typeof(Spot).GetProperty("AutoHwmMessageUnitBytes",
            BindingFlags.Instance | BindingFlags.Public));
        Assert.False(HasPublicInstanceMethod(typeof(Message),
            "GetPropertyString"));
        Assert.True(typeof(SocketBase).GetMethod(nameof(SocketBase.MonitorOpen))!
            .GetParameters()[0].HasDefaultValue);
        Assert.Equal(SocketEvent.All,
            typeof(SocketBase).GetMethod(nameof(SocketBase.MonitorOpen))!
                .GetParameters()[0].DefaultValue);
        Assert.True(HasPublicInstanceMethod(typeof(SocketBase),
            nameof(SocketBase.SetTlsServer), typeof(string), typeof(string),
            typeof(bool)));
        Assert.True(HasPublicInstanceMethod(typeof(SocketBase),
            nameof(SocketBase.SetTlsClient), typeof(string), typeof(string),
            typeof(bool)));
        Assert.True(HasPublicInstanceMethod(typeof(Discovery),
            nameof(Discovery.SetTlsClient), typeof(string), typeof(string),
            typeof(bool)));
        Assert.True(HasPublicInstanceMethod(typeof(Registry),
            nameof(Registry.SetHeartbeat), typeof(TimeSpan), typeof(TimeSpan)));
        Assert.True(HasPublicInstanceMethod(typeof(Registry),
            nameof(Registry.SetBroadcastInterval), typeof(TimeSpan)));
        Assert.True(HasPublicInstanceMethod(typeof(Registry),
            nameof(Registry.SetTlsServer), typeof(string), typeof(string),
            typeof(bool)));
        Assert.True(HasPublicInstanceMethod(typeof(Registry),
            nameof(Registry.SetTlsClient), typeof(string), typeof(string),
            typeof(bool)));
        Assert.False(HasPublicInstanceMethod(typeof(Registry),
            nameof(Registry.SetHeartbeat), typeof(uint), typeof(uint)));
        Assert.False(HasPublicInstanceMethod(typeof(Registry),
            nameof(Registry.SetBroadcastInterval), typeof(uint)));
        Assert.False(HasPublicInstanceMethod(typeof(Discovery), "MonitorOpen"));
    }

    [Fact]
    public void contract_interfaces_do_not_depend_on_runtime_concrete_types()
    {
        Assert.Equal(typeof(ISocketMonitor),
            typeof(ISocket).GetMethod(nameof(ISocket.MonitorOpen))!.ReturnType);
        Assert.Equal(typeof(IDiscovery),
            typeof(IDealerSocket).GetMethod(nameof(IDealerSocket.AttachDiscovery))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IDiscovery),
            typeof(IRouterSocket).GetMethod(nameof(IRouterSocket.AttachDiscovery))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IDiscovery),
            typeof(IPubSocket).GetMethod(nameof(IPubSocket.AttachDiscovery))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IDiscovery),
            typeof(ISubSocket).GetMethod(nameof(ISubSocket.AttachDiscovery))!
                .GetParameters()[0].ParameterType);

        Assert.Equal(typeof(ISpot),
            typeof(ISpotNode).GetMethod(nameof(ISpotNode.CreateSpot))!.ReturnType);
        Assert.Equal(typeof(ISpot),
            typeof(ISpotNode).GetMethod(nameof(ISpotNode.EntrySpot))!.ReturnType);
        Assert.Equal(typeof(ISpot),
            typeof(ISpotNode).GetMethod(nameof(ISpotNode.GetOrCreateSpot))!.ReturnType);
        Assert.Equal(typeof(ISpot),
            typeof(ISpotNode).GetMethod(nameof(ISpotNode.SpotLookup))!.ReturnType);
        Assert.Equal(typeof(IActor),
            typeof(ISpotNode).GetMethod(nameof(ISpotNode.CreateActor))!.ReturnType);
        Assert.Equal(typeof(IDiscovery),
            typeof(ISpotNode).GetMethod(nameof(ISpotNode.AttachDiscovery))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IDiscovery),
            typeof(ISpotNode).GetMethod(nameof(ISpotNode.AttachChannelDealer))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IDealerSocket),
            typeof(ISpotNode).GetMethod(nameof(ISpotNode.AttachChannelDealer))!
                .GetParameters()[1].ParameterType);
        Assert.Equal(typeof(IPubSocket),
            typeof(ISpotNode).GetMethod(nameof(ISpotNode.AttachPubIngress))!
                .GetParameters()[0].ParameterType);

        Assert.Equal(typeof(ISpot),
            typeof(IActor).GetMethod(nameof(IActor.Join))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(ISpot),
            typeof(IActor).GetMethod(nameof(IActor.Leave))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkTimer),
            typeof(IPoller).GetMethod(nameof(IPoller.Add),
                new[] { typeof(IZlinkTimer), typeof(object) })!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkTimer),
            typeof(IPoller).GetMethod(nameof(IPoller.Remove),
                new[] { typeof(IZlinkTimer) })!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(Action<IZlinkTimer, ulong>),
            typeof(IZlinkTimer).GetMethod(nameof(IZlinkTimer.OnFire))!
                .GetParameters()[0].ParameterType);
    }

    [Fact]
    public void actor_enums_match_core_contract_values()
    {
        Assert.Equal(105, (int)RequestResult.InternalError);
        Assert.Equal(106, (int)RequestResult.Rejected);
        Assert.Equal(107, (int)RequestResult.Conflict);
        Assert.Equal(108, (int)RequestResult.Busy);
        Assert.Equal(109, (int)RequestResult.NotConnected);
        Assert.Equal(110, (int)RequestResult.InvalidArgument);
        Assert.Equal(111, (int)RequestResult.InvalidState);
        Assert.Equal(112, (int)RequestResult.NotSupported);
        Assert.Equal(5, (int)SpotDispatchEvent.ActorReadable);
        Assert.Equal(6, (int)SpotDispatchEvent.ActorJoinReadable);
        Assert.Equal(4, (int)SpotDispatchSubjectKind.Actor);
    }

    [Fact]
    public void monitor_surface_exposes_canonical_receive_methods_only()
    {
        Assert.True(HasPublicInstanceMethod(typeof(SocketMonitor), "Recv"));
        Assert.False(HasPublicInstanceMethod(typeof(SocketMonitor), "Recv",
            typeof(bool)));
        Assert.False(HasPublicInstanceMethod(typeof(SocketMonitor), "TryRecv",
            typeof(SocketMonitorEvent).MakeByRefType()));
        Assert.True(HasPublicInstanceMethod(typeof(SocketMonitor),
            "OnEvent"));
        Assert.False(HasPublicInstanceMethod(typeof(SocketMonitor), "Receive"));
        Assert.False(HasPublicInstanceMethod(typeof(SocketMonitor), "TryReceive"));
        Assert.Equal(typeof(int), typeof(MonitorSnapshot)
            .GetProperty(nameof(MonitorSnapshot.AutoHwmEffectiveSndbuf))!
            .PropertyType);
        Assert.Equal(typeof(int), typeof(MonitorSnapshot)
            .GetProperty(nameof(MonitorSnapshot.AutoHwmEffectiveRcvbuf))!
            .PropertyType);
    }

    [Fact]
    public void request_reply_surface_uses_canonical_socket_methods()
    {
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            nameof(DealerSocket.Request)));
        Assert.False(HasPublicInstanceMethod(typeof(DealerSocket),
            "OnReceive"));
        Assert.True(HasPublicInstanceMethod(typeof(RouterSocket),
            nameof(RouterSocket.Request), typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(RouterSocket),
            nameof(RouterSocket.Reply), typeof(RoutingId), typeof(ulong)));
        Assert.False(HasPublicInstanceMethod(typeof(RouterSocket), "RecvSpot"));
        Assert.False(HasPublicInstanceMethod(typeof(RouterSocket), "OnSpotReceive"));
        Assert.True(HasPublicInstanceMethod(typeof(RoutedMessageSocketBase), "Send",
            typeof(RoutingId)));
        Assert.False(HasPublicInstanceMethod(typeof(RoutedMessageSocketBase), "Send",
            typeof(string), typeof(Message), typeof(SendFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(RoutedMessageSocketBase), "Recv",
            typeof(Received), typeof(RecvFlags)));
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
        Assert.DoesNotContain(exported, type => type.Name == "SocketOptions");
        Assert.DoesNotContain(exported, type => type.Name == "SocketOptionKey`1");
        Assert.DoesNotContain(exported, type => type.Name == "SocketOption");
        Assert.DoesNotContain(exported, type => type.Name == "ContextOption");
    }

    [Fact]
    public void exported_types_match_documented_public_contract()
    {
        string[] expected =
        {
            typeof(Actor).FullName!,
            typeof(ActorBindOperation).FullName!,
            typeof(ActorDestroyOperation).FullName!,
            typeof(ActorJoinInfo).FullName!,
            typeof(ActorJoinCallbackSubmitOperation).FullName!,
            typeof(ActorJoinHandler).FullName!,
            typeof(ActorJoinOperation).FullName!,
            typeof(ActorJoinReplyOperation).FullName!,
            typeof(ActorJoinRequest).FullName!,
            typeof(ActorJoinResult).FullName!,
            typeof(ActorJoinSubmitOperation).FullName!,
            typeof(ActorLeaveOperation).FullName!,
            typeof(ActorLifecycleHandler).FullName!,
            typeof(ActorLookupHandler).FullName!,
            typeof(ActorLookupOperation).FullName!,
            typeof(ActorLookupResult).FullName!,
            typeof(ActorPart).FullName!,
            typeof(ActorRecvInfo).FullName!,
            typeof(ActorRef).FullName!,
            typeof(ActorRoute).FullName!,
            typeof(ActorUnbindOperation).FullName!,
            typeof(AtomicCounter).FullName!,
            typeof(AutoConnectType).FullName!,
            typeof(AutoHwmProfile).FullName!,
            typeof(CommonSocketOptions).FullName!,
            typeof(ConnectableRoutedMessageSocketBase).FullName!,
            typeof(ConnectableSocketBase).FullName!,
            typeof(Context).FullName!,
            typeof(ContextOptions).FullName!,
            typeof(DealerSocket).FullName!,
            typeof(DealerSocketOptions).FullName!,
            typeof(Discovery).FullName!,
            typeof(IActor).FullName!,
            typeof(IAtomicCounter).FullName!,
            typeof(IConnectableRoutedMessageSocket).FullName!,
            typeof(IConnectableSocket).FullName!,
            typeof(IContext).FullName!,
            typeof(IContextOptions).FullName!,
            typeof(IDealerSocket).FullName!,
            typeof(IDiscovery).FullName!,
            typeof(IMessageSocket).FullName!,
            typeof(IPoller).FullName!,
            typeof(IPubSocket).FullName!,
            typeof(IPublisherSocket).FullName!,
            typeof(IRegistry).FullName!,
            typeof(IRegistryQueryClient).FullName!,
            typeof(IRouterSocket).FullName!,
            typeof(IRoutedMessageSocket).FullName!,
            typeof(ISocket).FullName!,
            typeof(ISocketMonitor).FullName!,
            typeof(ISpot).FullName!,
            typeof(ISpotNode).FullName!,
            typeof(IStreamSocket).FullName!,
            typeof(ISubSocket).FullName!,
            typeof(ISubscriberSocket).FullName!,
            typeof(IXPubSocket).FullName!,
            typeof(IXSubSocket).FullName!,
            typeof(IZlinkSocket).FullName!,
            typeof(IZlinkStopwatch).FullName!,
            typeof(IZlinkThread).FullName!,
            typeof(IZlinkTimer).FullName!,
            typeof(MemberPeerEntry).FullName!,
            typeof(Message).FullName!,
            typeof(MessageSocketBase).FullName!,
            typeof(MonitorEvent).FullName!,
            typeof(MonitorEventType).FullName!,
            typeof(MonitorSnapshot).FullName!,
            typeof(MonitorSourceKind).FullName!,
            typeof(OperationMessageExtensions).FullName!,
            typeof(PairSocket).FullName!,
            typeof(PollEvent).FullName!,
            typeof(PollEventFlags).FullName!,
            typeof(Poller).FullName!,
            typeof(PubSocket).FullName!,
            typeof(PubSocketOptions).FullName!,
            typeof(PublisherSocketBase).FullName!,
            typeof(Received).FullName!,
            typeof(RecvFlags).FullName!,
            typeof(Registry).FullName!,
            typeof(RegistryOption).FullName!,
            typeof(RegistryQueryClient).FullName!,
            typeof(RegistryServiceSummaryEntry).FullName!,
            typeof(RegistryServiceSummaryFilter).FullName!,
            typeof(RegistryState).FullName!,
            typeof(RegistryStatus).FullName!,
            typeof(RegistryTopologyEntry).FullName!,
            typeof(RegistryTopologyFilter).FullName!,
            typeof(ReplyOperation).FullName!,
            typeof(ReplyHandler).FullName!,
            typeof(ReplySubmitOperation).FullName!,
            typeof(RequestCallback).FullName!,
            typeof(RequestCallbackSubmitOperation).FullName!,
            typeof(RequestOperation).FullName!,
            typeof(RequestResult).FullName!,
            typeof(RequestSubmitOperation).FullName!,
            typeof(RidDuplicatePolicy).FullName!,
            typeof(RoutedMessageSocketBase).FullName!,
            typeof(RouterSocket).FullName!,
            typeof(RouterSocketOptions).FullName!,
            typeof(RoutingId).FullName!,
            typeof(SendFlags).FullName!,
            typeof(SendOperation).FullName!,
            typeof(SendSubmitOperation).FullName!,
            typeof(ServiceKind).FullName!,
            typeof(ServiceRole).FullName!,
            typeof(SocketBase).FullName!,
            typeof(SocketEvent).FullName!,
            typeof(SocketMonitor).FullName!,
            typeof(SocketType).FullName!,
            typeof(Spot).FullName!,
            typeof(SpotActorLifecycleInfo).FullName!,
            typeof(SpotDispatchEvent).FullName!,
            typeof(SpotDispatchInfo).FullName!,
            typeof(SpotDispatchSubjectKind).FullName!,
            typeof(SpotNode).FullName!,
            typeof(SpotNodeActorEntry).FullName!,
            typeof(SpotNodeMode).FullName!,
            typeof(SpotNodePeerEntry).FullName!,
            typeof(SpotNodePeerFilter).FullName!,
            typeof(SpotNodeSocketOwner).FullName!,
            typeof(SpotNodeSocketSnapshotEntry).FullName!,
            typeof(SpotNodeSocketSnapshotFilter).FullName!,
            typeof(SpotNodeSocketType).FullName!,
            typeof(SpotNodeSpotEntry).FullName!,
            typeof(SpotNodeState).FullName!,
            typeof(SpotNodeStatus).FullName!,
            typeof(SpotNodeSubjectEntry).FullName!,
            typeof(SpotNodeSubjectFilter).FullName!,
            typeof(SpotPeerSource).FullName!,
            typeof(SpotPeerState).FullName!,
            typeof(SpotRole).FullName!,
            typeof(StreamPacketHandler).FullName!,
            typeof(StreamSocket).FullName!,
            typeof(StreamSocketOptions).FullName!,
            typeof(SubSocket).FullName!,
            typeof(SubSocketOptions).FullName!,
            typeof(SubjectKind).FullName!,
            typeof(SubscriberSocketBase).FullName!,
            typeof(SubscriptionEntry).FullName!,
            typeof(SubscriptionEvent).FullName!,
            typeof(Timer).FullName!,
            typeof(TopicMessage).FullName!,
            typeof(TopologySource).FullName!,
            typeof(TopologyState).FullName!,
            typeof(XPubSocket).FullName!,
            typeof(XSubSocket).FullName!,
            typeof(ZlinkBindException).FullName!,
            typeof(ZlinkBindException.ErrorCode).FullName!,
            typeof(ZlinkCloseException).FullName!,
            typeof(ZlinkCloseException.ErrorCode).FullName!,
            typeof(ZlinkConfigException).FullName!,
            typeof(ZlinkConfigException.ErrorCode).FullName!,
            typeof(ZlinkConnectException).FullName!,
            typeof(ZlinkConnectException.ErrorCode).FullName!,
            typeof(ZlinkException).FullName!,
            typeof(ZlinkHandlerException).FullName!,
            typeof(ZlinkHandlerException.ErrorCode).FullName!,
            typeof(ZlinkPoll).FullName!,
            typeof(ZlinkRecvException).FullName!,
            typeof(ZlinkRecvException.ErrorCode).FullName!,
            typeof(ZlinkRequestException).FullName!,
            typeof(ZlinkRequestException.ErrorCode).FullName!,
            typeof(ZlinkStopwatch).FullName!,
            typeof(ZlinkSubmitException).FullName!,
            typeof(ZlinkSubmitException.ErrorCode).FullName!,
            typeof(ZlinkThread).FullName!,
            typeof(global::Systems.Zlink.Zlink).FullName!
        };

        string[] actual = typeof(SocketBase).Assembly.GetExportedTypes()
            .Select(type => type.FullName!)
            .OrderBy(name => name, StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(expected.OrderBy(name => name, StringComparer.Ordinal), actual);
    }

    [Fact]
    public void typed_option_facades_and_spot_surfaces_are_public()
    {
        Assert.Equal(typeof(CommonSocketOptions),
            typeof(SocketBase).GetProperty(nameof(SocketBase.Options))!.PropertyType);
        Assert.Equal(typeof(ContextOptions),
            typeof(Context).GetProperty(nameof(Context.Options))!.PropertyType);
        Assert.Equal(typeof(RouterSocketOptions),
            typeof(RouterSocket).GetProperty(nameof(RouterSocket.Options),
                BindingFlags.Instance | BindingFlags.Public
                | BindingFlags.DeclaredOnly)!
                .PropertyType);
        Assert.Equal(typeof(DealerSocketOptions),
            typeof(DealerSocket).GetProperty(nameof(DealerSocket.Options),
                BindingFlags.Instance | BindingFlags.Public
                | BindingFlags.DeclaredOnly)!
                .PropertyType);
        Assert.Equal(typeof(StreamSocketOptions),
            typeof(StreamSocket).GetProperty(nameof(StreamSocket.Options),
                BindingFlags.Instance | BindingFlags.Public
                | BindingFlags.DeclaredOnly)!
                .PropertyType);
        Assert.Equal(typeof(SubSocketOptions),
            typeof(SubSocket).GetProperty(nameof(SubSocket.Options),
                BindingFlags.Instance | BindingFlags.Public
                | BindingFlags.DeclaredOnly)!
                .PropertyType);
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "AttachChannelDealer",
            typeof(Discovery), typeof(DealerSocket)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "AttachChannelDealerManual",
            typeof(string), typeof(DealerSocket)));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode), "AttachPubIngress",
            typeof(PubSocket)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot), nameof(Spot.Publish),
            typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot), nameof(Spot.SendChannel),
            typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.RequestChannel), typeof(string)));
        Assert.False(HasPublicInstanceMethod(typeof(Spot), nameof(Spot.Publish),
            typeof(string), typeof(string), typeof(Message),
            typeof(SendFlags)));
        Assert.False(HasPublicInstanceMethod(typeof(Spot),
            "RequestChannelAsync", typeof(string), typeof(Message),
            typeof(TimeSpan), typeof(System.Threading.CancellationToken)));
        Assert.False(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.RequestChannel), typeof(string), typeof(Message),
            typeof(RequestCallback),
            typeof(SendFlags), typeof(TimeSpan?)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.ReceiveSubscriptionEvent), typeof(SubscriptionEvent),
            typeof(RecvFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(SubscriberSocketBase),
            nameof(SubscriberSocketBase.SubscriptionAt), typeof(int)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.SubscriptionAt), typeof(int)));
        var nullability = new NullabilityInfoContext();
        Assert.Equal(NullabilityState.Nullable,
            nullability.Create(typeof(SubscriberSocketBase)
                .GetMethod(nameof(SubscriberSocketBase.SubscriptionAt))!
                .ReturnParameter).ReadState);
        Assert.Equal(NullabilityState.Nullable,
            nullability.Create(typeof(Spot)
                .GetMethod(nameof(Spot.SubscriptionAt))!
                .ReturnParameter).ReadState);
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.OnDispatchEvent), typeof(Action<SpotDispatchInfo>)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.OnActorLifecycle),
            typeof(Action<SpotActorLifecycleInfo>),
            typeof(Action<SpotActorLifecycleInfo>)));
        Assert.Equal(typeof(ActorRef),
            typeof(SpotActorLifecycleInfo)
                .GetProperty(nameof(SpotActorLifecycleInfo.PreviousActor))!
                .PropertyType);
        Assert.Equal(typeof(ActorRef),
            typeof(SpotActorLifecycleInfo)
                .GetProperty(nameof(SpotActorLifecycleInfo.CurrentActor))!
                .PropertyType);
        Assert.False(HasPublicInstanceMethod(typeof(Spot),
            "DrainChannelReplyFrom", typeof(IntPtr)));
        Assert.Null(typeof(SpotDispatchInfo).GetProperty("Subject"));
        Assert.Equal(typeof(IZlinkTimer),
            typeof(SpotDispatchInfo).GetProperty(nameof(SpotDispatchInfo.Timer))!
                .PropertyType);
        Assert.True(HasPublicInstanceMethod(typeof(SpotDispatchInfo),
            nameof(SpotDispatchInfo.DrainChannelReply)));
        Assert.True(HasPublicInstanceMethod(typeof(SocketBase),
            "SetChannelName", typeof(string)));
        Assert.False(HasPublicInstanceMethod(typeof(SocketBase),
            "GetChannelName"));
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            nameof(DealerSocket.SetChannelName), typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(DealerSocket),
            nameof(DealerSocket.GetChannelName)));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket), "TryPublish",
            typeof(string), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(PubSocket), "TryPublish",
            typeof(string), typeof(IReadOnlyList<Message>)));
        Assert.False(HasPublicInstanceMethod(typeof(SubSocket), "TrySubscribe"));
        Assert.Null(typeof(DealerSocketOptions).GetProperty(
            nameof(DealerSocketOptions.Probe))!.GetMethod);
        Assert.NotNull(typeof(DealerSocketOptions).GetProperty(
            nameof(DealerSocketOptions.Probe))!.SetMethod);
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
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "Snapshot"));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "Peers"));
        Assert.False(HasPublicInstanceMethod(typeof(SpotNode), "Subjects"));
        Assert.True(HasPublicInstanceMethod(typeof(SpotNode),
            nameof(SpotNode.SetRoutingId), typeof(RoutingId)));
        Assert.Equal(typeof(RoutingId),
            typeof(SpotNode).GetProperty(nameof(SpotNode.RoutingId))!.PropertyType);

        Assert.False(HasPublicInstanceMethod(typeof(Discovery),
            "MemberPeerMetadata", typeof(ServiceRole), typeof(string)));
        ParameterInfo registryTopologyQueryFilter =
            typeof(Registry).GetMethod(nameof(Registry.TopologyQuery))!
                .GetParameters().Single();
        Assert.True(registryTopologyQueryFilter.HasDefaultValue);
        Assert.Null(registryTopologyQueryFilter.DefaultValue);
        Assert.True(HasPublicInstanceMethod(typeof(Discovery),
            nameof(Discovery.ResolveSpot), typeof(RoutingId)));
        Assert.False(HasPublicInstanceMethod(typeof(Discovery),
            "SetSpotOwnerSyncEnabled", typeof(bool)));
        Assert.False(HasPublicInstanceMethod(typeof(Discovery),
            "GetSpotOwnerSyncEnabled"));
        Assert.Equal(typeof(bool),
            typeof(Discovery).GetProperty(nameof(Discovery.SpotOwnerSyncEnabled))!
                .PropertyType);
        Assert.False(HasPublicInstanceMethod(typeof(Discovery),
            "SetActorRouteSyncEnabled", typeof(bool)));
        Assert.False(HasPublicInstanceMethod(typeof(Discovery),
            "GetActorRouteSyncEnabled"));
        Assert.Equal(typeof(bool),
            typeof(Discovery).GetProperty(nameof(Discovery.ActorRouteSyncEnabled))!
                .PropertyType);
        Assert.False(HasPublicInstanceMethod(typeof(Discovery), "BindRoute",
            typeof(uint), typeof(ReadOnlySpan<byte>), typeof(ReadOnlySpan<byte>)));
        Assert.False(HasPublicInstanceMethod(typeof(Discovery), "ResolveRoute",
            typeof(uint), typeof(ReadOnlySpan<byte>)));
        Assert.False(HasPublicInstanceMethod(typeof(Discovery),
            "SetDealerPeerMode"));
        Assert.False(HasPublicInstanceMethod(typeof(Discovery), "MonitorOpen"));
        Assert.False(HasPublicInstanceMethod(typeof(Discovery),
            "GetMemberPeerMetadata"));
        Assert.True(typeof(AutoConnectType).IsEnum);
        Assert.Equal(typeof(int), Enum.GetUnderlyingType(typeof(SubjectKind)));

        Assert.NotNull(typeof(RoutingId).GetProperty("IsEmpty",
            BindingFlags.Instance | BindingFlags.Public));
        Assert.True(HasPublicInstanceMethod(typeof(Message), "GetProperty",
            typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(SocketBase), "MonitorOpen",
            typeof(SocketEvent)));
        Assert.True(HasPublicInstanceMethod(typeof(Spot),
            nameof(Spot.SetRoutingId), typeof(RoutingId)));
        Assert.Equal(typeof(RoutingId),
            typeof(Spot).GetProperty(nameof(Spot.RoutingId))!.PropertyType);
    }

    [Fact]
    public void generic_socket_management_surface_uses_public_interface()
    {
        Assert.Equal(typeof(IZlinkSocket),
            typeof(global::Systems.Zlink.Zlink).GetMethod(
                nameof(global::Systems.Zlink.Zlink.Proxy))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(global::Systems.Zlink.Zlink).GetMethod(
                nameof(global::Systems.Zlink.Zlink.ProxySteerable))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(Poller).GetMethod(nameof(Poller.Add),
                new[] { typeof(IZlinkSocket), typeof(PollEventFlags), typeof(object) })!
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
        Assert.True(HasPublicInstanceMethod(typeof(Poller),
            nameof(Poller.Add), typeof(IZlinkTimer), typeof(object)));
        Assert.True(HasPublicInstanceMethod(typeof(Poller),
            nameof(Poller.Remove), typeof(IZlinkTimer)));
        Assert.True(HasPublicInstanceMethod(typeof(Poller),
            nameof(Poller.Wait), typeof(TimeSpan)));
        Assert.True(HasPublicInstanceMethod(typeof(Poller),
            nameof(Poller.Wait), typeof(Span<PollEvent>),
            typeof(TimeSpan), typeof(int).MakeByRefType()));
        Assert.Null(typeof(Poller).GetMethod(nameof(Poller.Wait),
            new[] { typeof(Span<PollEvent>), typeof(int), typeof(int).MakeByRefType() }));
        Assert.Equal(typeof(IZlinkSocket),
            typeof(PollEvent).GetProperty(nameof(PollEvent.Socket))!.PropertyType);
        Assert.Equal(typeof(IZlinkTimer),
            typeof(PollEvent).GetProperty(nameof(PollEvent.Timer))!.PropertyType);
        Assert.Equal(typeof(PollEventFlags),
            typeof(PollEvent).GetProperty(nameof(PollEvent.Revents))!.PropertyType);
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

    [Fact]
    public void monitor_and_submit_enums_include_admission_contract_values()
    {
        Assert.True(Enum.IsDefined(typeof(ZlinkSubmitException.ErrorCode),
            nameof(ZlinkSubmitException.ErrorCode.NotAdmitted)));
        Assert.Equal(13, (int)ZlinkSubmitException.ErrorCode.NotAdmitted);
        Assert.True(Enum.IsDefined(typeof(SocketEvent),
            nameof(SocketEvent.PeerWeightChanged)));
        Assert.Equal(0x8000, (int)SocketEvent.PeerWeightChanged);
        Assert.NotNull(typeof(SocketEvent).GetCustomAttribute<FlagsAttribute>());
        Assert.True(Enum.IsDefined(typeof(MonitorEventType),
            nameof(MonitorEventType.PeerWeightChanged)));
        Assert.Equal(0x8000, (int)MonitorEventType.PeerWeightChanged);
    }
}
