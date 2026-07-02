using System.Reflection;

namespace Zlink.Framework.Runtime.Configuration.Builders;

internal sealed class ZLinkFrameworkOptionsBuilder : IZLinkFrameworkOptions
{
    private readonly ZLinkFrameworkRegistration _registration;

    public ZLinkFrameworkOptionsBuilder(ZLinkFrameworkRegistration registration)
    {
        _registration = registration;
    }

    public TimeSpan DefaultRequestTimeout
    {
        get => _registration.DefaultRequestTimeout;
        set
        {
            ZLinkRequestTimeoutValidation.Validate(value, nameof(DefaultRequestTimeout));
            _registration.DefaultRequestTimeout = value;
        }
    }

    public TimeSpan? DefaultSocketSendTimeout
    {
        get => _registration.DefaultSocketSendTimeout;
        set
        {
            ZLinkSocketConfig.ValidateSendTimeout(value);
            _registration.DefaultSocketSendTimeout = value;
        }
    }

    public IZLinkCodecRegistryBuilder Codecs => _registration.Codecs;

    public IZLinkWorkerOptions Worker => _registration.WorkerOptions;

    public void AddHandlersFromAssemblyOf<TMarker>()
    {
        AddHandlersFromAssembly(typeof(TMarker).Assembly);
    }

    public void AddHandlersFromAssemblyOf(Type markerType)
    {
        ArgumentNullException.ThrowIfNull(markerType);

        AddHandlersFromAssembly(markerType.Assembly);
    }

    public void AddHandlersFromAssembly(Assembly assembly)
    {
        ArgumentNullException.ThrowIfNull(assembly);

        _registration.HandlerAssemblies.Add(assembly);
    }

    public IZLinkMetadataPolicyBuilder ConfigureMetadata()
    {
        return new ZLinkMetadataPolicyBuilder(_registration.MetadataPolicy);
    }

    public void AddSpotRemoteAddressResolver<TResolver>()
        where TResolver : class, IZLinkSpotRemoteAddressResolver
    {
        EnsureSpotRemoteAddressResolverAvailable();
        _registration.SpotRemoteAddressResolverType = typeof(TResolver);
    }

    public IZLinkClientServerChannelBuilder AddClientServerChannel(string channelName)
    {
        var channel = AddChannelRegistration(channelName, ZLinkAutoConnectType.ClientServer);
        return new ZLinkClientServerChannelBuilder(channel);
    }

    public IZLinkFanoutChannelBuilder AddFanoutChannel(string channelName)
    {
        var channel = AddChannelRegistration(channelName, ZLinkAutoConnectType.Fanout);
        return new ZLinkFanoutChannelBuilder(channel);
    }

    public IZLinkRouteMeshChannelBuilder AddRouteMesh(string channelName)
    {
        var routeChannel = ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.RouteChannels,
            channelName,
            () => new ZLinkRouteChannelRegistration { RouterChannelId = channelName },
            "Route mesh channel name must not be empty.",
            $"Duplicate route mesh channel name '{channelName}'.");

        return new ZLinkRouteChannelBuilder(routeChannel);
    }

    public void AddPeerLocationStore<TStore>()
        where TStore : class, IZLinkPeerLocationStore
    {
        _registration.Locations.PeerStoreType = typeof(TStore);
    }

    public void AddSpotLocationStore<TStore>()
        where TStore : class, IZLinkSpotLocationStore
    {
        _registration.Locations.SpotStoreType = typeof(TStore);
    }

    public void AddActorLocationStore<TStore>()
        where TStore : class, IZLinkActorLocationStore
    {
        _registration.Locations.ActorStoreType = typeof(TStore);
    }

    public void AddRouteLocationStore<TStore>()
        where TStore : class, IZLinkRouteLocationStore
    {
        _registration.Locations.RouteStoreType = typeof(TStore);
    }

    public void AddOwnerLeaseStore<TStore>()
        where TStore : class, IZLinkOwnerLeaseStore
    {
        _registration.Locations.OwnerLeaseStoreType = typeof(TStore);
    }

    public void UseInMemoryLocationStores()
    {
        _registration.Locations.UseInMemoryStores = true;
    }

    public ZLinkLocationOptions ConfigureLocations()
    {
        return _registration.Locations.Options;
    }

    public void UseFilter<TFilter>()
        where TFilter : class, IZLinkHandlerFilter
    {
        _registration.Filters.Add(typeof(TFilter));
    }

    public IZLinkDispatchOptions ConfigureDispatch()
    {
        return _registration.DispatchOptions;
    }

    public IZLinkStreamCompressionBuilder ConfigureStreamCompression()
    {
        return new ZLinkStreamCompressionBuilder(_registration);
    }

    public IZLinkStreamNodeBuilder AddStreamNode(string streamNodeName)
    {
        var streamNode = ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.StreamNodes,
            streamNodeName,
            () => new ZLinkStreamNodeRegistration { StreamNodeName = streamNodeName },
            "STREAM node name must not be empty.",
            $"Duplicate stream node name '{streamNodeName}'.");

        return new ZLinkStreamNodeBuilder(streamNode);
    }

    public IZLinkSpotMeshBuilder AddSpotMesh(string channelName)
    {
        if (string.IsNullOrWhiteSpace(channelName))
            throw new ZLinkConfigurationException("SPOT mesh channel name must not be empty.");

        var discovery = ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.SpotDiscoveries,
            channelName,
            () => new ZLinkSpotDiscoveryRegistration
            {
                ChannelName = channelName
            },
            "SPOT mesh channel name must not be empty.",
            $"Duplicate SPOT mesh channel name '{channelName}'.");
        var spotNode = ZLinkRegistrationBuilderGuard.RegisterSpotNode(
            _registration.SpotNodes,
            channelName);
        spotNode.SpotDiscoveryChannelName = discovery.ChannelName;

        return new ZLinkSpotMeshBuilder(spotNode);
    }

    private ZLinkChannelRegistration AddChannelRegistration(
        string channelName,
        ZLinkAutoConnectType autoConnectType)
    {
        return ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.Channels,
            channelName,
            () => new ZLinkChannelRegistration
            {
                ChannelName = channelName,
                AutoConnectType = autoConnectType
            },
            "Channel name must not be empty.",
            $"Duplicate channel name '{channelName}'.");
    }

    private void EnsureSpotRemoteAddressResolverAvailable()
    {
        if (_registration.SpotRemoteAddressResolverType is not null)
            throw new ZLinkConfigurationException("SPOT remote address resolver is already registered.");
    }
}

internal sealed class ZLinkSpotMeshBuilder(
    ZLinkSpotNodeRegistration spotNode)
    : IZLinkSpotMeshBuilder
{
    private ZLinkSpotNodeBuilder? _nodeBuilder;

    public IZLinkSpotNodeBuilder EnableRouter(string endpoint)
    {
        return DefaultNode().EnableRouter(endpoint);
    }

    public IZLinkSpotNodeBuilder ConnectRouter(string endpoint)
    {
        return DefaultNode().ConnectRouter(endpoint);
    }

    public IZLinkSpotNodeBuilder ConnectRouter(RoutingId peerRid, string endpoint)
    {
        return DefaultNode().ConnectRouter(peerRid, endpoint);
    }

    public IZLinkSpotNodeBuilder SetRoutingId(RoutingId routingId)
    {
        return DefaultNode().SetRoutingId(routingId);
    }

    public IZLinkSocketConfig ConfigureRouterSocket()
    {
        return DefaultNode().ConfigureRouterSocket();
    }

    public IZLinkRouteConfig ConfigureRouterRouting()
    {
        return DefaultNode().ConfigureRouterRouting();
    }

    public IZLinkSpotNodeBuilder EnablePubSub(string endpoint)
    {
        return DefaultNode().EnablePubSub(endpoint);
    }

    public IZLinkSpotNodeBuilder ConnectPeerPub(string endpoint)
    {
        return DefaultNode().ConnectPeerPub(endpoint);
    }

    public IZLinkSpotPublisherConfig ConfigurePubSubPublisher()
    {
        return DefaultNode().ConfigurePubSubPublisher();
    }

    public IZLinkSpotSubscriberConfig ConfigurePubSubSubscriber()
    {
        return DefaultNode().ConfigurePubSubSubscriber();
    }

    public IZLinkEntrySpotOptions ConfigureEntrySpot()
    {
        return DefaultNode().ConfigureEntrySpot();
    }

    public IZLinkSpotNodeBuilder SetEntrySpotRoutingId(RoutingId routingId)
    {
        return DefaultNode().SetEntrySpotRoutingId(routingId);
    }

    public IZLinkSpotNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot
    {
        return DefaultNode().AddSpotFactory<TSpot>();
    }

    public IZLinkSpotNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot
    {
        return DefaultNode().AddEntrySpot<TEntrySpot>();
    }

    public IZLinkSpotNodeBuilder AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory
    {
        return DefaultNode().AddActorFactory<TFactory>(actorType);
    }

    private ZLinkSpotNodeBuilder DefaultNode()
    {
        return _nodeBuilder ??= new ZLinkSpotNodeBuilder(spotNode);
    }
}

internal static class ZLinkRegistrationBuilderGuard
{
    public static ZLinkSpotNodeRegistration RegisterSpotNode(
        Dictionary<string, ZLinkSpotNodeRegistration> registrations,
        string spotNodeName)
    {
        return AddUnique(
            registrations,
            spotNodeName,
            () => new ZLinkSpotNodeRegistration { SpotNodeName = spotNodeName },
            "SPOT node name must not be empty.",
            $"Duplicate spot node name '{spotNodeName}'.");
    }

    public static void AddUnique<TValue>(
        Dictionary<string, TValue> registrations,
        string name,
        TValue value,
        string emptyMessage,
        string duplicateMessage)
    {
        AddUnique(registrations, name, () => value, emptyMessage, duplicateMessage);
    }

    public static TValue AddUnique<TValue>(
        Dictionary<string, TValue> registrations,
        string name,
        Func<TValue> create,
        string emptyMessage,
        string duplicateMessage)
    {
        if (string.IsNullOrWhiteSpace(name)) throw new ZLinkConfigurationException(emptyMessage);

        var value = create();
        if (!registrations.TryAdd(name, value)) throw new ZLinkConfigurationException(duplicateMessage);

        return value;
    }
}

internal sealed class ZLinkMetadataPolicyBuilder(ZLinkMetadataPolicyRegistration registration)
    : IZLinkMetadataPolicyBuilder
{
    public void AddForwardedMetadataKey(string key)
    {
        if (string.IsNullOrWhiteSpace(key)) throw new ZLinkConfigurationException("Metadata key must not be empty.");

        registration.ForwardedApplicationKeys.Add(key);
    }
}
