namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkSpotNodeBuilder(ZLinkSpotNodeRegistration registration)
    : IZLinkSpotNodeBuilder,
        IZLinkSpotMeshBuilder
{
    public IZLinkEndpointConnections RouterConnections => EnsureRouter().ManualConnections;

    public IZLinkEndpointConnections PubSubConnections => EnsurePubSub().ManualConnections;

    public IZLinkEndpointConnections ChannelClientConnections => RouterConnections;

    public IZLinkEndpointConnections PublisherConnections => PubSubConnections;

    public IZLinkSpotMeshBuilder UseDrainPolicy(ZLinkSpotDrainPolicy policy)
    {
        if (!Enum.IsDefined(policy))
            throw new ZLinkConfigurationException($"Unknown SPOT drain policy '{policy}'.");

        registration.DrainPolicy = policy;
        return this;
    }

    public IZLinkSpotNodeBuilder EnableRouter(string endpoint)
    {
        var router = EnsureRouter();
        if (string.IsNullOrWhiteSpace(endpoint))
            throw new ZLinkConfigurationException("SPOT router bind endpoint must not be empty.");

        router.BindEndpoint = endpoint;
        return this;
    }

    public IZLinkSpotNodeBuilder ConnectRouter(string endpoint)
    {
        AddRouterManualConnection(endpoint, null);
        return this;
    }

    public IZLinkSpotNodeBuilder ConnectRouter(RoutingId peerRid, string endpoint)
    {
        if (peerRid.Size == 0)
            throw new ZLinkConfigurationException("Manual SPOT router peer routing id must not be empty.");

        AddRouterManualConnection(endpoint, peerRid);
        return this;
    }

    public IZLinkSpotNodeBuilder SetRoutingId(RoutingId routingId)
    {
        registration.RoutingId = routingId;
        registration.HasExplicitRoutingId = true;
        return this;
    }

    public IZLinkSpotNodeBuilder UseAllocatedRoutingId(int slotCount) =>
        UseAllocatedRoutingId(slotCount, registration.SpotNodeName);

    public IZLinkSpotNodeBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix)
    {
        registration.RoutingIdAllocation = ZLinkRoutingIdAllocationBuilderSupport.Create(
            slotCount,
            routingIdPrefix,
            registration.RoutingIdAllocation?.GroupName);
        return this;
    }

    public IZLinkSpotNodeBuilder SetRoutingIdAllocationGroup(string groupName)
    {
        registration.RoutingIdAllocation = ZLinkRoutingIdAllocationBuilderSupport.WithGroup(
            groupName,
            registration.RoutingIdAllocation,
            registration.SpotNodeName);
        return this;
    }

    public IZLinkSocketConfig ConfigureRouterSocket()
    {
        return EnsureRouter().SocketConfig;
    }

    public IZLinkRouteConfig ConfigureRouterRouting()
    {
        return EnsureRouter().RoutingConfig;
    }

    public IZLinkSpotNodeBuilder EnablePubSub(string endpoint)
    {
        var pubSub = EnsurePubSub();
        if (string.IsNullOrWhiteSpace(endpoint))
            throw new ZLinkConfigurationException("SPOT pub/sub bind endpoint must not be empty.");

        pubSub.BindEndpoint = endpoint;
        return this;
    }

    public IZLinkSpotNodeBuilder ConnectPeerPub(string endpoint)
    {
        ZLinkChannelEndpointBuilderSupport.AddManualConnection(
            EnsurePubSub().ManualConnections,
            endpoint,
            "Manual SPOT peer PUB endpoint must not be empty.");
        return this;
    }

    public IZLinkSpotPublisherConfig ConfigurePubSubPublisher()
    {
        return EnsurePubSub().PublisherConfig;
    }

    public IZLinkSpotSubscriberConfig ConfigurePubSubSubscriber()
    {
        return EnsurePubSub().SubscriberConfig;
    }

    public IZLinkEntrySpotOptions ConfigureEntrySpot()
    {
        return registration.EntrySpotOptions;
    }

    public IZLinkSpotNodeBuilder SetEntrySpotRoutingId(RoutingId routingId)
    {
        registration.EntrySpotOptions.RoutingId = routingId;
        registration.HasExplicitEntrySpotRoutingId = true;
        return this;
    }

    public IZLinkSpotNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot
    {
        if (!registration.SpotFactories.Add(typeof(TSpot)))
            throw new ZLinkConfigurationException(
                $"Duplicate SPOT factory '{typeof(TSpot)}' on node '{registration.SpotNodeName}'.");

        return this;
    }

    public IZLinkSpotNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot
    {
        if (registration.EntrySpotType is not null)
            throw new ZLinkConfigurationException(
                $"Duplicate Entry Spot registry on node '{registration.SpotNodeName}'.");

        registration.EntrySpotType = typeof(TEntrySpot);
        return this;
    }

    public IZLinkSpotNodeBuilder AddActorFactory<TFactory>(string actorType)
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

    public IZLinkSpotNodeBuilder AddActorTransferAdapter<TActor, TAdapter>(string actorType)
        where TActor : IZLinkActor
        where TAdapter : class, IZLinkActorTransferAdapter<TActor>
    {
        AddActorTransfer(
            actorType,
            ZLinkActorTransferRegistry.CreateRegistration<TActor, TAdapter>());
        return this;
    }

    private void AddActorTransfer(
        string actorType,
        ZLinkActorTransferRegistration transfer)
    {
        ZLinkRegistrationBuilderGuard.AddUnique(
            registration.ActorTransfers,
            actorType,
            transfer,
            "Actor transfer name must not be empty.",
            $"Duplicate actor transfer '{actorType}'.");
    }

    private ZLinkSpotRouterCapabilityRegistration EnsureRouter()
    {
        registration.Router ??= new ZLinkSpotRouterCapabilityRegistration();
        return registration.Router;
    }

    private void AddRouterManualConnection(string endpoint, RoutingId? peerRid)
    {
        if (string.IsNullOrWhiteSpace(endpoint))
            throw new ZLinkConfigurationException("Manual SPOT router endpoint must not be empty.");

        var router = EnsureRouter();
        router.ManualConnections.Connect(endpoint);
        if (peerRid is { } routingId) router.PeerRoutingIds[endpoint] = routingId;
    }

    private ZLinkSpotPubSubCapabilityRegistration EnsurePubSub()
    {
        registration.PubSub ??= new ZLinkSpotPubSubCapabilityRegistration();
        return registration.PubSub;
    }
}
