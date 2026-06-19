using System;
using System.Linq;
using System.Reflection;
using Xunit;

namespace Systems.Zlink.Tests;

public sealed class test_socket_surface
{
    private static MethodInfo[] PublicInstanceMethods(Type type)
    {
        return type.IsInterface
            ? type.GetInterfaces()
                .Append(type)
                .SelectMany(current =>
                    current.GetMethods(BindingFlags.Instance | BindingFlags.Public))
                .ToArray()
            : type.GetMethods(BindingFlags.Instance | BindingFlags.Public);
    }

    private static bool HasPublicInstanceMethod(Type type, string name,
        params Type[] parameterTypes)
    {
        return PublicInstanceMethods(type).Any(method =>
            method.Name == name
            && method.GetParameters().Select(p => p.ParameterType)
                .SequenceEqual(parameterTypes));
    }

    private static bool HasPublicStaticMethod(Type type, string name,
        params Type[] parameterTypes)
    {
        return type.GetMethods(BindingFlags.Static | BindingFlags.Public)
            .Any(method => method.Name == name
                && method.GetParameters().Select(p => p.ParameterType)
                    .SequenceEqual(parameterTypes));
    }

    private static void AssertNoPublicInstanceMethod(Type type, string name)
    {
        Assert.DoesNotContain(PublicInstanceMethods(type),
            method => method.Name == name);
    }

    private static void AssertNullableReturn(Type type, string methodName)
    {
        MethodInfo method = PublicInstanceMethods(type)
            .FirstOrDefault(method => method.Name == methodName)
            ?? throw new InvalidOperationException(
                $"{type.Name}.{methodName} was not found.");
        var nullability = new NullabilityInfoContext();
        Assert.Equal(NullabilityState.Nullable,
            nullability.Create(method.ReturnParameter).ReadState);
    }

    [Fact]
    public void runtime_implementation_types_are_not_public_contract()
    {
        string[] hiddenRuntimeTypeNames =
        {
            "Systems.Zlink.Context",
            "Systems.Zlink.SocketBase",
            "Systems.Zlink.ConnectableSocketBase",
            "Systems.Zlink.MessageSocketBase",
            "Systems.Zlink.PublisherSocketBase",
            "Systems.Zlink.RoutedMessageSocketBase",
            "Systems.Zlink.ConnectableRoutedMessageSocketBase",
            "Systems.Zlink.SubscriberSocketBase",
            "Systems.Zlink.PairSocket",
            "Systems.Zlink.DealerSocket",
            "Systems.Zlink.RouterSocket",
            "Systems.Zlink.PubSocket",
            "Systems.Zlink.SubSocket",
            "Systems.Zlink.XPubSocket",
            "Systems.Zlink.XSubSocket",
            "Systems.Zlink.StreamSocket",
            "Systems.Zlink.SocketMonitor",
            "Systems.Zlink.Poller",
            "Systems.Zlink.Timer",
            "Systems.Zlink.Registry",
            "Systems.Zlink.RegistryQueryClient",
            "Systems.Zlink.Discovery",
            "Systems.Zlink.SpotNode",
            "Systems.Zlink.Spot",
            "Systems.Zlink.Actor",
            "Systems.Zlink.AtomicCounter",
            "Systems.Zlink.ZlinkStopwatch",
            "Systems.Zlink.ZlinkThread"
        };

        string[] exportedTypeNames = typeof(Zlink).Assembly.GetExportedTypes()
            .Select(type => type.FullName!)
            .ToArray();

        foreach (string typeName in hiddenRuntimeTypeNames)
        {
            Assert.DoesNotContain(typeName, exportedTypeNames);
        }
    }

    [Fact]
    public void implementation_namespaces_are_not_public_contract()
    {
        string[] exportedTypeNames = typeof(Zlink).Assembly.GetExportedTypes()
            .Select(type => type.FullName!)
            .ToArray();

        Assert.DoesNotContain(exportedTypeNames,
            typeName => typeName.StartsWith("Systems.Zlink.Runtime.Native.",
                StringComparison.Ordinal));
        Assert.DoesNotContain(exportedTypeNames,
            typeName => typeName.StartsWith("Systems.Zlink.Runtime.",
                StringComparison.Ordinal));
    }

    [Fact]
    public void context_factory_exposes_public_resource_contracts()
    {
        Assert.Equal(typeof(IContext),
            typeof(Zlink).GetMethod(nameof(Zlink.CreateContext))!.ReturnType);
        Assert.Equal(typeof(IAtomicCounter),
            typeof(Zlink).GetMethod(nameof(Zlink.CreateAtomicCounter))!.ReturnType);
        Assert.Equal(typeof(IZlinkStopwatch),
            typeof(Zlink).GetMethod(nameof(Zlink.CreateStopwatch))!.ReturnType);
        Assert.Equal(typeof(IZlinkThread),
            typeof(Zlink).GetMethod(nameof(Zlink.CreateThread))!.ReturnType);
        Assert.Null(typeof(Zlink).GetMethod("StartStopwatch"));
        Assert.Null(typeof(Zlink).GetMethod("StartThread"));
        Assert.Equal(typeof(IPoller),
            typeof(Zlink).GetMethod(nameof(Zlink.CreatePoller), Type.EmptyTypes)!
                .ReturnType);
        Assert.Equal(typeof(IZlinkTimer),
            typeof(Zlink).GetMethod(nameof(Zlink.CreateTimer), Type.EmptyTypes)!
                .ReturnType);

        Assert.Equal(typeof(IPairSocket),
            typeof(IContext).GetMethod(nameof(IContext.CreatePairSocket))!
                .ReturnType);
        Assert.Equal(typeof(IDealerSocket),
            typeof(IContext).GetMethod(nameof(IContext.CreateDealerSocket))!
                .ReturnType);
        Assert.Equal(typeof(IRouterSocket),
            typeof(IContext).GetMethod(nameof(IContext.CreateRouterSocket))!
                .ReturnType);
        Assert.Equal(typeof(IPubSocket),
            typeof(IContext).GetMethod(nameof(IContext.CreatePubSocket))!
                .ReturnType);
        Assert.Equal(typeof(ISubSocket),
            typeof(IContext).GetMethod(nameof(IContext.CreateSubSocket))!
                .ReturnType);
        Assert.Equal(typeof(IXPubSocket),
            typeof(IContext).GetMethod(nameof(IContext.CreateXPubSocket))!
                .ReturnType);
        Assert.Equal(typeof(IXSubSocket),
            typeof(IContext).GetMethod(nameof(IContext.CreateXSubSocket))!
                .ReturnType);
        Assert.Equal(typeof(IStreamSocket),
            typeof(IContext).GetMethod(nameof(IContext.CreateStreamSocket))!
                .ReturnType);
        Assert.Equal(typeof(IRegistry),
            typeof(IContext).GetMethod(nameof(IContext.CreateRegistry))!
                .ReturnType);
        Assert.Equal(typeof(IRegistryQueryClient),
            typeof(IContext).GetMethod(nameof(IContext.CreateRegistryQueryClient))!
                .ReturnType);
        Assert.Equal(typeof(IDiscovery),
            typeof(IContext).GetMethod(nameof(IContext.CreateDiscovery))!
                .ReturnType);
        Assert.Equal(typeof(ISpotNode),
            typeof(IContext).GetMethod(nameof(IContext.CreateSpotNode),
                Type.EmptyTypes)!.ReturnType);
    }

    [Fact]
    public void typed_contract_surface_hides_irrelevant_methods()
    {
        AssertNoPublicInstanceMethod(typeof(IPubSocket), "Recv");
        AssertNoPublicInstanceMethod(typeof(IPubSocket), "Subscribe");
        AssertNoPublicInstanceMethod(typeof(ISubSocket), "Publish");
        Assert.False(HasPublicInstanceMethod(typeof(IPairSocket), "Send",
            typeof(string), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(IDealerSocket), "Send",
            typeof(string), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(IRouterSocket), "Send",
            typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(IStreamSocket), "Send",
            typeof(Message)));
        AssertNoPublicInstanceMethod(typeof(IStreamSocket), "Connect");
        AssertNoPublicInstanceMethod(typeof(IStreamSocket), "Disconnect");
        AssertNoPublicInstanceMethod(typeof(ISubSocket), "OnSendReady");
        AssertNoPublicInstanceMethod(typeof(IXSubSocket), "OnSendReady");
        AssertNoPublicInstanceMethod(typeof(IPairSocket), "SetOption");
        AssertNoPublicInstanceMethod(typeof(IPairSocket), "GetOption");
        AssertNoPublicInstanceMethod(typeof(IPairSocket), "AttachDiscovery");
        AssertNoPublicInstanceMethod(typeof(IPairSocket), "OnReceive");
        AssertNoPublicInstanceMethod(typeof(IDealerSocket), "OnReceive");
        AssertNoPublicInstanceMethod(typeof(IRouterSocket), "OnReceive");
        AssertNoPublicInstanceMethod(typeof(IStreamSocket), "OnReceive");
        AssertNoPublicInstanceMethod(typeof(IPairSocket), "OnSubscribe");
        AssertNoPublicInstanceMethod(typeof(ISubSocket), "OnSubscribe");
        AssertNoPublicInstanceMethod(typeof(IXSubSocket), "OnSubscribe");
        AssertNoPublicInstanceMethod(typeof(IStreamSocket), "OnFramedPacket");
        AssertNoPublicInstanceMethod(typeof(IDealerSocket), "ReceiveSubscriptionEvent");
        AssertNoPublicInstanceMethod(typeof(IPubSocket), "ReceiveSubscriptionEvent");

        Assert.True(HasPublicInstanceMethod(typeof(IPairSocket), "Recv",
            typeof(Received), typeof(RecvFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(IDealerSocket), "Recv",
            typeof(Received), typeof(RecvFlags)));
        AssertNoPublicInstanceMethod(typeof(IRouterSocket), "RecvPart");
        Assert.False(HasPublicInstanceMethod(typeof(IRouterSocket), "Send",
            typeof(RoutingId), typeof(Message), typeof(SendFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(IStreamSocket), "Recv",
            typeof(Received), typeof(RecvFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(IStreamSocket),
            nameof(IStreamSocket.OnPacket), typeof(StreamPacketHandler)));
        Assert.True(HasPublicInstanceMethod(typeof(IStreamSocket),
            nameof(IStreamSocket.SetRoutingId), typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(IStreamSocket),
            nameof(IStreamSocket.GetRoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(IStreamSocket),
            nameof(IStreamSocket.DisconnectRid), typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(IPairSocket),
            nameof(IConnectableSocket.DisconnectRid), typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(IRouterSocket),
            nameof(IConnectableSocket.DisconnectRid), typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(IStreamSocket),
            nameof(IStreamSocket.AttachActorGateway), typeof(ISpotNode)));
        Assert.True(HasPublicInstanceMethod(typeof(IStreamSocket),
            nameof(IStreamSocket.BindActor), typeof(RoutingId),
            typeof(ActorRef)));
        Assert.True(HasPublicInstanceMethod(typeof(IStreamSocket),
            nameof(IStreamSocket.UnbindActor), typeof(RoutingId),
            typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(IStreamSocket),
            nameof(IStreamSocket.SendBoundActor), typeof(RoutingId),
            typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(IStreamSocket),
            nameof(IStreamSocket.BoundActors), typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(IDealerSocket),
            nameof(IDealerSocket.AttachDiscovery), typeof(IDiscovery)));
        Assert.True(HasPublicInstanceMethod(typeof(IRouterSocket),
            nameof(IRouterSocket.AttachDiscovery), typeof(IDiscovery)));
        Assert.True(HasPublicInstanceMethod(typeof(IPubSocket),
            nameof(IPubSocket.AttachDiscovery), typeof(IDiscovery)));
        Assert.True(HasPublicInstanceMethod(typeof(ISubSocket),
            nameof(ISubSocket.AttachDiscovery), typeof(IDiscovery)));

        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(IContext)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(ISocket)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(ISocketMonitor)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(Message)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(IPoller)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(IAtomicCounter)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(IZlinkStopwatch)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(IRegistry)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(IDiscovery)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(IRegistryQueryClient)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(ISpotNode)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(ISpot)));
        Assert.True(typeof(IAsyncDisposable).IsAssignableFrom(typeof(IActor)));
    }

    [Fact]
    public void public_surface_removes_raw_flags_and_option_bags()
    {
        Type[] socketContracts =
        {
            typeof(ISocket),
            typeof(IConnectableSocket),
            typeof(IMessageSocket),
            typeof(IPairSocket),
            typeof(IDealerSocket),
            typeof(IRouterSocket),
            typeof(IPublisherSocket),
            typeof(IPubSocket),
            typeof(ISubscriberSocket),
            typeof(ISubSocket),
            typeof(IXPubSocket),
            typeof(IXSubSocket),
            typeof(IStreamSocket)
        };

        foreach (Type socketType in socketContracts)
        {
            AssertNoPublicInstanceMethod(socketType, "TrySend");
            AssertNoPublicInstanceMethod(socketType, "TryRecv");
            AssertNoPublicInstanceMethod(socketType, "TryPublish");
            AssertNoPublicInstanceMethod(socketType, "TryRequest");
            AssertNoPublicInstanceMethod(socketType, "TrySubscribe");
            AssertNoPublicInstanceMethod(socketType, "SendNoWait");
            AssertNoPublicInstanceMethod(socketType, "RecvNoWait");
            AssertNoPublicInstanceMethod(socketType, "RequestAsync");
            AssertNoPublicInstanceMethod(socketType, "RequestCallback");
            AssertNoPublicInstanceMethod(socketType, "SetOption");
            AssertNoPublicInstanceMethod(socketType, "GetOption");
        }

        string[] exportedTypeNames = typeof(Zlink).Assembly.GetExportedTypes()
            .Select(type => type.FullName!)
            .ToArray();
        Assert.DoesNotContain("Systems.Zlink.SocketOption", exportedTypeNames);
        Assert.DoesNotContain("Systems.Zlink.SocketOptions", exportedTypeNames);

        Assert.True(typeof(DealerSocketOptions)
            .GetProperty(nameof(DealerSocketOptions.Probe))!.CanWrite);
        Assert.False(typeof(DealerSocketOptions)
            .GetProperty(nameof(DealerSocketOptions.Probe))!.CanRead);

        AssertNullableReturn(typeof(ISubscriberSocket),
            nameof(ISubscriberSocket.SubscriptionAt));
        AssertNullableReturn(typeof(ISpot), nameof(ISpot.SubscriptionAt));
    }

    [Fact]
    public void service_surface_uses_contract_interfaces()
    {
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode), "Status"));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode), "Peers"));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode), "PeersQuery",
            typeof(SpotNodePeerFilter)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.DisconnectPeerRid), typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.ConnectRouterChannelPeer), typeof(string),
            typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.DisconnectRouterChannelPeer), typeof(string),
            typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.DisconnectRouterChannelPeerRid), typeof(string),
            typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.AttachSpotRouteChannelDiscovery), typeof(string),
            typeof(IDiscovery)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.AttachChannelDealer), typeof(IDiscovery),
            typeof(IDealerSocket)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.AttachChannelDealerManual), typeof(string),
            typeof(IDealerSocket)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.AttachPubIngress), typeof(IPubSocket)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.CreateActor), typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.ActorLookup), typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.GetOrCreateSpot), typeof(RoutingId),
            typeof(bool).MakeByRefType()));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.RemoteActorGetRef), typeof(RoutingId),
            typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.JoinActor), typeof(ActorRef), typeof(RoutingId),
            typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.JoinActorEntrySpot), typeof(ActorRef),
            typeof(RoutingId), typeof(Message)));
        Assert.False(HasPublicInstanceMethod(typeof(ISpotNode),
            nameof(ISpotNode.JoinActorEntrySpot), typeof(ActorRef),
            typeof(RoutingId)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpot),
            nameof(ISpot.RecvActorJoin), typeof(RecvFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpot),
            nameof(ISpot.ReplyActorJoin), typeof(ActorJoinRequest), typeof(int)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpot),
            nameof(ISpot.ReceiveSubscriptionEvent), typeof(SubscriptionEvent),
            typeof(RecvFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpot),
            nameof(ISpot.RecvRouted), typeof(Received), typeof(RecvFlags)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpot),
            nameof(ISpot.DrainReplies)));
        AssertNoPublicInstanceMethod(typeof(ISpot), "RecvRoutedPart");
        AssertNoPublicInstanceMethod(typeof(ISpot), "SubscribePart");
        Assert.True(HasPublicInstanceMethod(typeof(ISpot),
            nameof(ISpot.SendToChannel), typeof(string)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpot),
            nameof(ISpot.RequestToChannel), typeof(string)));
        AssertNoPublicInstanceMethod(typeof(ISpot), "SendChannel");
        AssertNoPublicInstanceMethod(typeof(ISpot), "RequestChannel");
        Assert.True(HasPublicInstanceMethod(typeof(ISpot),
            nameof(ISpot.SetDispatchHandler), typeof(SpotDispatchHandler)));
        Assert.True(HasPublicInstanceMethod(typeof(ISpot),
            nameof(ISpot.RecvActorLifecycle), typeof(RecvFlags)));
        AssertNoPublicInstanceMethod(typeof(ISpot), "SetActorLifecycleHandlers");
        AssertNoPublicInstanceMethod(typeof(ISpot), "SetRoutedReceiveHandler");
        Assert.False(HasPublicInstanceMethod(typeof(ISpot), "Publish",
            typeof(string), typeof(Message), typeof(SendFlags)));
        Assert.False(HasPublicInstanceMethod(typeof(ISpot), "SendToSpot",
            typeof(RoutingId), typeof(RoutingId), typeof(Message),
            typeof(SendFlags)));
        Assert.Null(typeof(ISpot).GetMethod("RequestToChannelAsync"));
        Assert.Null(typeof(ISpot).GetMethod("DrainChannelReplyFrom"));

        Assert.False(HasPublicInstanceMethod(typeof(IDiscovery),
            "MemberPeerMetadata", typeof(ServiceRole), typeof(string)));
        Assert.False(HasPublicInstanceMethod(typeof(IDiscovery),
            "SetSpotOwnerSyncEnabled", typeof(bool)));
        Assert.False(HasPublicInstanceMethod(typeof(IDiscovery),
            "GetSpotOwnerSyncEnabled"));
        Assert.Equal(typeof(bool),
            typeof(IDiscovery).GetProperty(nameof(IDiscovery.SpotOwnerSyncEnabled))!
                .PropertyType);
        Assert.True(HasPublicInstanceMethod(typeof(IDiscovery), "BindRoute",
            typeof(uint), typeof(ReadOnlySpan<byte>), typeof(ReadOnlySpan<byte>)));
        Assert.True(HasPublicInstanceMethod(typeof(IDiscovery), "UnbindRoute",
            typeof(uint), typeof(ReadOnlySpan<byte>)));
        Assert.True(HasPublicInstanceMethod(typeof(IDiscovery), "ResolveRoute",
            typeof(uint), typeof(ReadOnlySpan<byte>)));
        Assert.True(HasPublicInstanceMethod(typeof(IDiscovery),
            nameof(IDiscovery.ResolveActor), typeof(string)));
        AssertNoPublicInstanceMethod(typeof(IDiscovery), "SetDealerPeerMode");
        AssertNoPublicInstanceMethod(typeof(IDiscovery), "MonitorOpen");
        AssertNoPublicInstanceMethod(typeof(IDiscovery), "GetMemberPeerMetadata");
        Assert.Equal(typeof(bool),
            typeof(IDiscovery).GetProperty(
                nameof(IDiscovery.ActorRouteSyncEnabled))!.PropertyType);

        Assert.NotNull(typeof(AutoConnectType));
        Assert.Equal(typeof(int), Enum.GetUnderlyingType(typeof(SubjectKind)));
        Assert.Equal(typeof(string),
            typeof(Message).GetMethod(nameof(Message.GetProperty))!
                .ReturnParameter.ParameterType);

        ParameterInfo registryTopologyFilter =
            typeof(IRegistry).GetMethod(nameof(IRegistry.Topology))!
                .GetParameters().Single();
        Assert.True(registryTopologyFilter.HasDefaultValue);
        Assert.Null(registryTopologyFilter.DefaultValue);

        Assert.Equal(typeof(ActorRef),
            typeof(SpotActorLifecycleInfo)
                .GetProperty(nameof(SpotActorLifecycleInfo.PreviousActor))!
                .PropertyType);
        Assert.Equal(typeof(ActorRef),
            typeof(SpotActorLifecycleInfo)
                .GetProperty(nameof(SpotActorLifecycleInfo.CurrentActor))!
                .PropertyType);
        Assert.Null(typeof(SpotDispatchInfo).GetProperty("Subject"));
        Assert.Equal(typeof(IZlinkTimer),
            typeof(SpotDispatchInfo).GetProperty(nameof(SpotDispatchInfo.Timer))!
                .PropertyType);
        Assert.True(HasPublicInstanceMethod(typeof(SpotDispatchInfo),
            nameof(SpotDispatchInfo.DrainChannelReply)));
    }

    [Fact]
    public void routing_id_factory_names_are_canonical()
    {
        Assert.False(HasPublicStaticMethod(typeof(RoutingId), "Of",
            typeof(string)));
        Assert.False(HasPublicStaticMethod(typeof(RoutingId), "FromUtf8",
            typeof(string)));
        Assert.False(HasPublicStaticMethod(typeof(RoutingId), "FromString",
            typeof(string)));
        Assert.False(HasPublicStaticMethod(typeof(RoutingId), "FromBytes",
            typeof(byte[])));
        Assert.False(HasPublicStaticMethod(typeof(RoutingId), "FromUInt32",
            typeof(uint)));
        Assert.False(HasPublicStaticMethod(typeof(RoutingId), "FromGuid",
            typeof(Guid)));
        Assert.True(HasPublicStaticMethod(typeof(RoutingId), "From",
            typeof(string)));
        Assert.True(HasPublicStaticMethod(typeof(RoutingId), "From",
            typeof(byte[])));
        Assert.True(HasPublicStaticMethod(typeof(RoutingId), "From",
            typeof(ReadOnlySpan<byte>)));
        Assert.True(HasPublicStaticMethod(typeof(RoutingId), "FromHex",
            typeof(string)));
        Assert.True(HasPublicStaticMethod(typeof(RoutingId), "From",
            typeof(uint)));
        Assert.True(HasPublicStaticMethod(typeof(RoutingId), "From",
            typeof(Guid)));
        Assert.True(HasPublicInstanceMethod(typeof(RoutingId), "TryToUInt32",
            typeof(uint).MakeByRefType()));
        Assert.True(HasPublicInstanceMethod(typeof(RoutingId), "TryToGuid",
            typeof(Guid).MakeByRefType()));
        Assert.NotNull(typeof(RoutingId).GetProperty("IsEmpty",
            BindingFlags.Instance | BindingFlags.Public));
    }

    [Fact]
    public void message_operation_extension_type_uses_canonical_name()
    {
        string[] exportedTypeNames = typeof(Zlink).Assembly.GetExportedTypes()
            .Select(type => type.FullName!)
            .ToArray();

        Assert.Contains("Systems.Zlink.MessageOperations", exportedTypeNames);
        Assert.DoesNotContain("Systems.Zlink.OperationMessageExtensions",
            exportedTypeNames);
    }

    [Fact]
    public void received_uses_factory_without_public_constructor()
    {
        Assert.Null(typeof(Received).GetConstructor(Type.EmptyTypes));
        Assert.Equal(typeof(Received),
            typeof(Received).GetMethod(nameof(Received.Create),
                Type.EmptyTypes)!.ReturnType);
        Assert.True(HasPublicInstanceMethod(typeof(Received),
            nameof(Received.FirstPart)));
        Assert.True(HasPublicInstanceMethod(typeof(Received),
            nameof(Received.SinglePartOrThrow)));
        Assert.True(HasPublicInstanceMethod(typeof(Received),
            nameof(Received.Reply)));
        Assert.True(HasPublicInstanceMethod(typeof(Received),
            nameof(Received.Send)));
    }

    [Fact]
    public void generic_management_surface_uses_public_interfaces()
    {
        Assert.Equal(typeof(IZlinkSocket),
            typeof(Zlink).GetMethod(nameof(Zlink.Proxy))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(Zlink).GetMethod(nameof(Zlink.ProxySteerable))!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(IPoller).GetMethod(nameof(IPoller.Add),
                new[] { typeof(IZlinkSocket), typeof(PollEventFlags),
                    typeof(nuint) })!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(IPoller).GetMethod(nameof(IPoller.Modify),
                new[] { typeof(IZlinkSocket), typeof(PollEventFlags) })!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(IZlinkSocket),
            typeof(IPoller).GetMethod(nameof(IPoller.Remove),
                new[] { typeof(IZlinkSocket) })!
                .GetParameters()[0].ParameterType);
        Assert.Equal(typeof(int),
            typeof(IPoller).GetProperty(nameof(IPoller.Size))!.PropertyType);
        Assert.True(HasPublicInstanceMethod(typeof(IPoller),
            nameof(IPoller.Add), typeof(IZlinkTimer), typeof(nuint)));
        Assert.True(HasPublicInstanceMethod(typeof(IPoller),
            nameof(IPoller.Wait), typeof(Span<PollEvent>), typeof(TimeSpan)));
        Assert.Null(typeof(IPoller).GetMethod(nameof(IPoller.Wait),
            new[] { typeof(TimeSpan) }));
        Assert.Null(typeof(IPoller).GetMethod(nameof(IPoller.Wait),
            new[] { typeof(Span<PollEvent>), typeof(int),
                typeof(int).MakeByRefType() }));
        Assert.Null(typeof(PollEvent).GetProperty("Socket"));
        Assert.Null(typeof(PollEvent).GetProperty("Timer"));
        Assert.Null(typeof(PollEvent).GetProperty("Tag"));
        Assert.Null(typeof(PollEvent).GetProperty("Events"));
        Assert.Equal(typeof(PollSourceKind),
            typeof(PollEvent).GetProperty(nameof(PollEvent.SourceKind))!
                .PropertyType);
        Assert.Equal(typeof(nuint),
            typeof(PollEvent).GetProperty(nameof(PollEvent.Slot))!
                .PropertyType);
        Assert.Equal(typeof(PollEventFlags),
            typeof(PollEvent).GetProperty(nameof(PollEvent.Revents))!
                .PropertyType);
    }
}
