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

    public TimeSpan ActorTransferForwardWindow
    {
        get => _registration.ActorTransferForwardWindow;
        set
        {
            if (value < TimeSpan.Zero)
                throw new ArgumentOutOfRangeException(
                    nameof(value),
                    value,
                    "Actor transfer forward window must not be negative.");
            _registration.ActorTransferForwardWindow = value;
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

    public void DisableImplicitHandlerAutoRegistration()
    {
        _registration.ImplicitHandlerAutoRegistrationEnabled = false;
    }

    public IZLinkMetadataPolicyBuilder ConfigureMetadata()
    {
        return new ZLinkMetadataPolicyBuilder(_registration.MetadataPolicy);
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

    public IZLinkRouteMeshChannelBuilder AddRouteMeshChannel(string channelName)
    {
        var routeChannel = ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.RouteChannels,
            channelName,
            () => new ZLinkRouteChannelRegistration { RouterChannelId = channelName },
            "Route mesh channel name must not be empty.",
            $"Duplicate route mesh channel name '{channelName}'.");

        return new ZLinkRouteChannelBuilder(routeChannel);
    }

    // Test-only in-memory location store registration (spec 05-route-mesh §7 /
    // gap 90 §12.33 S8-08). Not on IZLinkFrameworkOptions; single-process contract
    // tests reach it through the internal ZLinkTestLocationStores helper. Production
    // hosts register a distributed store via AddLocationStore or fail fast.
    internal void UseInMemoryLocationStores()
    {
        _registration.Locations.UseInMemoryStores = true;
    }

    public void AddLocationStore(IZLinkLocationStore store)
    {
        ArgumentNullException.ThrowIfNull(store);

        _registration.Locations.StoreInstance = store;
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

    public IZLinkMeshNodeBuilder AddRouteMesh(string meshName)
    {
        if (string.IsNullOrWhiteSpace(meshName))
            throw new ZLinkConfigurationException("RouteMesh name must not be empty.");

        // A MeshNode drives the same registration the runtime consumes: a mesh
        // channel keyed by meshName (the discovery/location identity) plus the
        // spot node keyed by the same meshName (spec 05-route-mesh §2).
        var discovery = ZLinkRegistrationBuilderGuard.AddUnique(
            _registration.SpotMeshChannels,
            meshName,
            () => new ZLinkSpotMeshChannelRegistration
            {
                ChannelName = meshName
            },
            "RouteMesh name must not be empty.",
            $"Duplicate RouteMesh name '{meshName}'.");
        var meshNode = ZLinkRegistrationBuilderGuard.RegisterSpotNode(
            _registration.SpotNodes,
            meshName);
        meshNode.SpotMeshChannelName = discovery.ChannelName;

        return new ZLinkMeshNodeBuilder(meshNode);
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
