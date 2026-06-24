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

    public void AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory
    {
        ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.ActorFactories,
            actorType,
            typeof(TFactory),
            "Actor factory name must not be empty.",
            $"Duplicate actor factory '{actorType}'.");
    }

    public void AddSpotRemoteAddressResolver<TResolver>()
        where TResolver : class, IZLinkSpotRemoteAddressResolver
    {
        EnsureSpotRemoteAddressResolverAvailable();
        _registration.SpotRemoteAddressResolverType = typeof(TResolver);
    }

    public IZLinkRegistrySpotRemoteAddressesOptions UseRegistrySpotRemoteAddresses(
        string namespaceName)
    {
        EnsureSpotRemoteAddressResolverAvailable();

        var options = new ZLinkRegistrySpotRemoteAddressesRegistration
        {
            Namespace = ValidateRegistryNamespace(namespaceName),
        };
        _registration.RegistrySpotRemoteAddresses = options;
        return options;
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

    public IZLinkDiscoveryBuilder UseDiscovery()
    {
        _registration.Discovery ??= new ZLinkDiscoveryRegistration();
        return new ZLinkDiscoveryBuilder(_registration.Discovery.Endpoints);
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
        {
            throw new ZLinkConfigurationException("SPOT mesh channel name must not be empty.");
        }

        if (_registration.SpotDiscovery is not null)
        {
            throw new ZLinkConfigurationException("SPOT mesh is already configured.");
        }

        var discovery = new ZLinkSpotDiscoveryRegistration
        {
            ChannelName = channelName,
        };
        var spotNode = ZLinkRegistrationBuilderGuard.RegisterSpotNode(
            _registration.SpotNodes,
            channelName);

        _registration.SpotDiscovery = discovery;
        return new ZLinkSpotMeshBuilder(_registration, discovery, spotNode);
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
                AutoConnectType = autoConnectType,
            },
            "Channel name must not be empty.",
            $"Duplicate channel name '{channelName}'.");
    }

    private void EnsureSpotRemoteAddressResolverAvailable()
    {
        if (_registration.SpotRemoteAddressResolverType is not null
            || _registration.RegistrySpotRemoteAddresses is not null)
        {
            throw new ZLinkConfigurationException("SPOT remote address resolver is already registered.");
        }
    }

    private static string ValidateRegistryNamespace(string namespaceName)
    {
        if (string.IsNullOrWhiteSpace(namespaceName)
            || !string.Equals(namespaceName, namespaceName.Trim(), StringComparison.Ordinal))
        {
            throw new ZLinkConfigurationException("Registry route namespace must not be empty or padded.");
        }

        return namespaceName;
    }

}

internal sealed class ZLinkSpotMeshBuilder(
    ZLinkFrameworkRegistration registration,
    ZLinkSpotDiscoveryRegistration discovery,
    ZLinkSpotNodeRegistration spotNode)
    : IZLinkSpotMeshBuilder
{
    private ZLinkSpotNodeBuilder? _nodeBuilder;

    public IZLinkDiscoveryBuilder UseDiscovery()
    {
        return new ZLinkDiscoveryBuilder(discovery.Endpoints);
    }

    public IZLinkSpotMeshBuilder UseRegistrySpotResolver()
    {
        if (registration.SpotRemoteAddressResolverType is not null
            || registration.RegistrySpotRemoteAddresses is not null)
        {
            throw new ZLinkConfigurationException("SPOT remote address resolver is already registered.");
        }

        registration.RegistrySpotRemoteAddresses = new ZLinkRegistrySpotRemoteAddressesRegistration
        {
            Namespace = discovery.ChannelName,
            RouterChannelId = discovery.ChannelName,
        };
        return this;
    }

    public IZLinkSpotNodeBuilder EnableRouter(string endpoint)
        => DefaultNode().EnableRouter(endpoint);

    public IZLinkSpotNodeBuilder ConnectRouter(string endpoint)
        => DefaultNode().ConnectRouter(endpoint);

    public IZLinkSpotNodeBuilder ConnectRouter(RoutingId peerRid, string endpoint)
        => DefaultNode().ConnectRouter(peerRid, endpoint);

    public IZLinkSpotNodeBuilder SetRouterRoutingId(RoutingId routingId)
        => DefaultNode().SetRouterRoutingId(routingId);

    public IZLinkSocketConfig ConfigureRouterSocket()
        => DefaultNode().ConfigureRouterSocket();

    public IZLinkRouteConfig ConfigureRouterRouting()
        => DefaultNode().ConfigureRouterRouting();

    public IZLinkSpotNodeBuilder EnablePubSub(string endpoint)
        => DefaultNode().EnablePubSub(endpoint);

    public IZLinkSpotNodeBuilder ConnectPeerPub(string endpoint)
        => DefaultNode().ConnectPeerPub(endpoint);

    public IZLinkSpotNodeBuilder ConnectPubSub(string endpoint)
        => DefaultNode().ConnectPubSub(endpoint);

    public IZLinkSpotNodeBuilder SetPubSubRoutingId(RoutingId routingId)
        => DefaultNode().SetPubSubRoutingId(routingId);

    public IZLinkSpotPublisherConfig ConfigurePubSubPublisher()
        => DefaultNode().ConfigurePubSubPublisher();

    public IZLinkSpotSubscriberConfig ConfigurePubSubSubscriber()
        => DefaultNode().ConfigurePubSubSubscriber();

    public IZLinkEntrySpotOptions ConfigureEntrySpot()
        => DefaultNode().ConfigureEntrySpot();

    public IZLinkSpotNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot
        => DefaultNode().AddSpotFactory<TSpot>();

    public IZLinkSpotNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot
        => DefaultNode().AddEntrySpot<TEntrySpot>();

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
        if (string.IsNullOrWhiteSpace(name))
        {
            throw new ZLinkConfigurationException(emptyMessage);
        }

        var value = create();
        if (!registrations.TryAdd(name, value))
        {
            throw new ZLinkConfigurationException(duplicateMessage);
        }

        return value;
    }
}

internal sealed class ZLinkMetadataPolicyBuilder(ZLinkMetadataPolicyRegistration registration)
    : IZLinkMetadataPolicyBuilder
{
    public void AddForwardedMetadataKey(string key)
    {
        if (string.IsNullOrWhiteSpace(key))
        {
            throw new ZLinkConfigurationException("Metadata key must not be empty.");
        }

        registration.ForwardedApplicationKeys.Add(key);
    }
}
