namespace Zlink.Framework.Runtime.Configuration.Builders;

// MeshNode builder (spec 05-route-mesh §2). Drives the same
// ZLinkSpotNodeRegistration the runtime consumes: AddRouteMesh(meshName) owns the
// node's single ROUTER endpoint, its logical channel memberships, RID-direct route
// handlers, Spot/Actor registry, publish admission policy and drain policy.
internal sealed class ZLinkMeshNodeBuilder(ZLinkSpotNodeRegistration registration)
    : IZLinkMeshNodeBuilder
{
    private ZLinkMeshPeerConnections? _peerConnections;

    public IZLinkMeshChannelBuilder ChannelName(string channelName)
    {
        if (string.IsNullOrWhiteSpace(channelName))
            throw new ZLinkConfigurationException("Channel membership name must not be empty.");

        if (registration.ChannelMemberships.Any(
                membership => string.Equals(membership.ChannelName, channelName, StringComparison.Ordinal)))
            throw new ZLinkConfigurationException(
                $"Duplicate channel membership '{channelName}' on MeshNode '{registration.SpotNodeName}'.");

        var membership = new ZLinkMeshChannelMembership { ChannelName = channelName };
        registration.ChannelMemberships.Add(membership);
        return new ZLinkMeshChannelBuilder(membership);
    }

    public IZLinkMeshNodeBuilder Listen(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
            throw new ZLinkConfigurationException("MeshNode ROUTER bind endpoint must not be empty.");

        EnsureRouter().BindEndpoint = endpoint;
        return this;
    }

    public IZLinkMeshNodeBuilder SetRoutingId(RoutingId routingId)
    {
        registration.RoutingId = routingId;
        registration.HasExplicitRoutingId = true;
        return this;
    }

    public IZLinkMeshNodeBuilder UseAllocatedRoutingId(int slotCount) =>
        UseAllocatedRoutingId(slotCount, registration.SpotNodeName);

    public IZLinkMeshNodeBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix)
    {
        registration.RoutingIdAllocation = ZLinkRoutingIdAllocationBuilderSupport.Create(
            slotCount,
            routingIdPrefix,
            registration.RoutingIdAllocation?.GroupName);
        return this;
    }

    public IZLinkMeshNodeBuilder SetRoutingIdAllocationGroup(string groupName)
    {
        registration.RoutingIdAllocation = ZLinkRoutingIdAllocationBuilderSupport.WithGroup(
            groupName,
            registration.RoutingIdAllocation,
            registration.SpotNodeName);
        return this;
    }

    public IZLinkMeshNodeSocketConfig ConfigureRouterSocket() => EnsureRouter().SocketConfig;

    public IZLinkSpotPublisherConfig ConfigureSpotPublisher() => registration.SpotPublisherConfig;

    public IZLinkMeshPeerConnections PeerConnections =>
        _peerConnections ??= new ZLinkMeshPeerConnections(EnsureRouter());

    public IZLinkMeshNodeBuilder SetDefaultRequestTimeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        registration.DefaultRequestTimeout = timeout;
        return this;
    }

    public IZLinkMeshNodeBuilder AddRouteSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>
    {
        registration.RouteSendHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            typeof(TMessage),
            null,
            packetName));
        return this;
    }

    public IZLinkMeshNodeBuilder AddRouteSendHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        var args = ZLinkTypedHandlerBuilderSupport.ResolveSingleHandlerInterface(
                typeof(THandler),
                typeof(IZLinkRouteSendHandler<>),
                "route send")
            .GetGenericArguments();
        registration.RouteSendHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            args[0],
            null,
            packetName));
        return this;
    }

    public IZLinkMeshNodeBuilder AddRouteRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>
    {
        registration.RouteRequestHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            typeof(TRequest),
            typeof(TReply),
            packetName));
        return this;
    }

    public IZLinkMeshNodeBuilder AddRouteRequestHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        var args = ZLinkTypedHandlerBuilderSupport.ResolveSingleHandlerInterface(
                typeof(THandler),
                typeof(IZLinkRouteRequestHandler<,>),
                "route request")
            .GetGenericArguments();
        registration.RouteRequestHandlers.Add(new ZLinkRouteHandlerRegistration(
            typeof(THandler),
            args[0],
            args[1],
            packetName));
        return this;
    }

    public IZLinkEntrySpotOptions ConfigureEntrySpot() => registration.EntrySpotOptions;

    public IZLinkMeshNodeBuilder SetEntrySpotRoutingId(RoutingId routingId)
    {
        registration.EntrySpotOptions.RoutingId = routingId;
        registration.HasExplicitEntrySpotRoutingId = true;
        return this;
    }

    public IZLinkMeshNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot
    {
        if (!registration.SpotFactories.Add(typeof(TSpot)))
            throw new ZLinkConfigurationException(
                $"Duplicate SPOT factory '{typeof(TSpot)}' on MeshNode '{registration.SpotNodeName}'.");

        return this;
    }

    public IZLinkMeshNodeBuilder AddInstanceSpotFactory<TSpot>(
        string instanceSpotType,
        ZLinkInstanceSpotFactoryOptions? options = null)
        where TSpot : class, IZLinkInstanceSpot
    {
        if (string.IsNullOrWhiteSpace(instanceSpotType))
            throw new ZLinkConfigurationException(
                "Instance Spot type must not be empty.");
        if (System.Text.Encoding.UTF8.GetByteCount(instanceSpotType) > 255
            || instanceSpotType.Contains('\0'))
            throw new ZLinkConfigurationException(
                "Instance Spot type must be 1 to 255 UTF-8 bytes without NUL.");

        var effective = options ?? new ZLinkInstanceSpotFactoryOptions();
        if (effective.MaxActiveInstances <= 0)
            throw new ZLinkConfigurationException(
                "MaxActiveInstances must be greater than zero.");
        if (effective.ActivationTimeout <= TimeSpan.Zero)
            throw new ZLinkConfigurationException(
                "ActivationTimeout must be greater than zero.");
        if (!registration.InstanceSpotFactories.TryAdd(
                instanceSpotType,
                new ZLinkInstanceSpotFactoryRegistration(
                    typeof(TSpot),
                    effective)))
            throw new ZLinkConfigurationException(
                $"Duplicate Instance Spot factory '{instanceSpotType}' on "
                + $"MeshNode '{registration.SpotNodeName}'.");

        return this;
    }

    public IZLinkMeshNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot
    {
        if (registration.EntrySpotType is not null)
            throw new ZLinkConfigurationException(
                $"Duplicate Entry Spot registry on MeshNode '{registration.SpotNodeName}'.");

        registration.EntrySpotType = typeof(TEntrySpot);
        return this;
    }

    public IZLinkMeshNodeBuilder AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory
    {
        ZLinkRegistrationBuilderGuard.AddUnique(
            registration.ActorFactories,
            actorType,
            typeof(TFactory),
            "Actor factory name must not be empty.",
            $"Duplicate actor factory '{actorType}'.");
        return this;
    }

    public IZLinkMeshNodeBuilder AddActorTransferAdapter<TActor, TAdapter>(string actorType)
        where TActor : IZLinkActor
        where TAdapter : class, IZLinkActorTransferAdapter<TActor>
    {
        ZLinkRegistrationBuilderGuard.AddUnique(
            registration.ActorTransfers,
            actorType,
            ZLinkActorTransferRegistry.CreateRegistration<TActor, TAdapter>(),
            "Actor transfer name must not be empty.",
            $"Duplicate actor transfer '{actorType}'.");
        return this;
    }

    private ZLinkSpotRouterCapabilityRegistration EnsureRouter()
    {
        registration.Router ??= new ZLinkSpotRouterCapabilityRegistration();
        return registration.Router;
    }
}

// Logical channel membership builder (spec 05-route-mesh §4). Weight and the
// channel-scoped IZLinkSendHandler/IZLinkRequestHandler namespace are recorded on
// the membership the MeshNode owns.
internal sealed class ZLinkMeshChannelBuilder(ZLinkMeshChannelMembership membership)
    : IZLinkMeshChannelBuilder
{
    public IZLinkMeshChannelBuilder SetWeight(int weight)
    {
        membership.Weight = weight;
        return this;
    }

    public IZLinkMeshChannelBuilder AddHandlerGroup(string groupName)
    {
        ZLinkHandlerGroupBuilderSupport.AddHandlerGroup(membership.HandlerGroups, groupName);
        return this;
    }

    public IZLinkMeshChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>
    {
        membership.SendHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            typeof(TMessage),
            null,
            packetName));
        return this;
    }

    public IZLinkMeshChannelBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        var args = ZLinkTypedHandlerBuilderSupport.ResolveSingleHandlerInterface(
                typeof(THandler),
                typeof(IZLinkSendHandler<>),
                "send")
            .GetGenericArguments();
        membership.SendHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            args[0],
            null,
            packetName));
        return this;
    }

    public IZLinkMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>
    {
        membership.RequestHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            typeof(TRequest),
            typeof(TReply),
            packetName));
        return this;
    }

    public IZLinkMeshChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class
    {
        var args = ZLinkTypedHandlerBuilderSupport.ResolveSingleHandlerInterface(
                typeof(THandler),
                typeof(IZLinkRequestHandler<,>),
                "request")
            .GetGenericArguments();
        membership.RequestHandlers.Add(new ZLinkChannelHandlerRegistration(
            typeof(THandler),
            args[0],
            args[1],
            packetName));
        return this;
    }
}

// Adapter over the MeshNode ROUTER's manual connection set and expected peer RIDs
// (spec 05-route-mesh §3 IZLinkMeshPeerConnections). Expected RID is optional; when
// present it pins the admission handshake identity for that endpoint.
internal sealed class ZLinkMeshPeerConnections(ZLinkSpotRouterCapabilityRegistration router)
    : IZLinkMeshPeerConnections
{
    public void Connect(string endpoint)
    {
        ValidateEndpoint(endpoint);
        router.ManualConnections.Connect(endpoint);
    }

    public void Connect(RoutingId expectedRoutingId, string endpoint)
    {
        if (expectedRoutingId.Size == 0)
            throw new ZLinkConfigurationException("Expected peer routing id must not be empty.");

        ValidateEndpoint(endpoint);
        if (router.PeerRoutingIds.TryGetValue(endpoint, out var previousRid)
            && previousRid != expectedRoutingId
            && router.ManualConnections.ListConnections().Contains(endpoint, StringComparer.Ordinal))
            router.ManualConnections.Disconnect(endpoint);
        router.PeerRoutingIds[endpoint] = expectedRoutingId;
        try
        {
            router.ManualConnections.Connect(endpoint);
        }
        catch
        {
            router.PeerRoutingIds.Remove(endpoint);
            throw;
        }
    }

    public void Disconnect(string endpoint)
    {
        ValidateEndpoint(endpoint);
        router.ManualConnections.Disconnect(endpoint);
        router.PeerRoutingIds.Remove(endpoint);
    }

    public IReadOnlyList<ZLinkMeshPeerConnection> ListConnections()
    {
        return router.ManualConnections.ListConnections()
            .Select(endpoint => new ZLinkMeshPeerConnection(
                endpoint,
                router.PeerRoutingIds.TryGetValue(endpoint, out var rid) ? rid : null))
            .ToArray();
    }

    private static void ValidateEndpoint(string endpoint)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
            throw new ZLinkConfigurationException("Manual MeshNode peer endpoint must not be empty.");
    }
}
