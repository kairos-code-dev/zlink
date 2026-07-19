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

    public TimeSpan? ActorTransferTimeout
    {
        get => _registration.ActorTransferTimeout;
        set
        {
            ValidateOptionalPositiveTimeout(value, nameof(ActorTransferTimeout));
            _registration.ActorTransferTimeout = value;
        }
    }

    public TimeSpan? ActorTransferForwardWindow
    {
        get => _registration.ActorTransferForwardWindow;
        set
        {
            ValidateOptionalPositiveTimeout(value, nameof(ActorTransferForwardWindow));
            _registration.ActorTransferForwardWindow = value;
        }
    }

    public TimeSpan DefaultSocketSendTimeout
    {
        get => _registration.DefaultSocketSendTimeout;
        set
        {
            ZLinkSocketConfig.ValidateSendTimeout(value);
            _registration.DefaultSocketSendTimeout = value;
        }
    }

    private static void ValidateOptionalPositiveTimeout(TimeSpan? value, string name)
    {
        if (value is { } timeout && timeout <= TimeSpan.Zero)
            throw new ArgumentOutOfRangeException(
                name,
                value,
                "Actor transfer timeout values must be greater than zero.");
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

    public IZLinkFanoutChannelBuilder AddFanoutChannel(string channelName)
    {
        var channel = AddChannelRegistration(channelName, ZLinkAutoConnectType.Fanout);
        return new ZLinkFanoutChannelBuilder(channel);
    }

    // Test-only in-memory location store registration (spec 05-route-mesh §7 /
    // gap 90 §12.33 S8-08). Not on IZLinkFrameworkOptions; single-process contract
    // tests reach it through the internal ZLinkTestLocationStores helper. Production
    // hosts register a distributed store via AddLocationStore or fail fast.
    internal void UseTestLocationStore()
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
    public IZLinkMetadataPolicyBuilder AllowSessionToActor(string key)
    {
        AddKey(registration.SessionToActorKeys, key);
        return this;
    }

    public IZLinkMetadataPolicyBuilder AllowActorToSession(string key)
    {
        AddKey(registration.ActorToSessionKeys, key);
        return this;
    }

    private static void AddKey(HashSet<string> keys, string key)
    {
        if (string.IsNullOrWhiteSpace(key))
            throw new ZLinkConfigurationException("Metadata key must not be empty.");

        keys.Add(key);
    }
}
