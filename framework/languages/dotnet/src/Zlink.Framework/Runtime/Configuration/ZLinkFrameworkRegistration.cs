using System.Reflection;
using System.Collections.Frozen;

namespace Zlink.Framework.Runtime.Configuration;

internal sealed class ZLinkFrameworkRegistration
{
    private ZLinkScannedHandlerCatalog? _scannedHandlerCatalog;

    public TimeSpan DefaultRequestTimeout { get; set; } = TimeSpan.FromSeconds(30);

    public TimeSpan? ActorTransferTimeout { get; set; }

    public TimeSpan? ActorTransferForwardWindow { get; set; }

    public TimeSpan DefaultSocketSendTimeout { get; set; } = TimeSpan.FromMilliseconds(1000);

    public ZLinkCodecRegistryBuilder Codecs { get; } = new();

    public IZlinkStreamCompressionCodec? StreamCompressionCodec { get; set; } =
        ZLinkStreamProtocolDefaults.CreateLz4CompressionCodec();

    public ZLinkMetadataPolicyRegistration MetadataPolicy { get; } = new();

    public ZLinkDispatchOptionsModel DispatchOptions { get; } = new();

    public ZLinkWorkerOptionsModel WorkerOptions { get; } = new();

    public List<Type> Filters { get; } = [];

    public ZLinkLocationRegistration Locations { get; } = new();

    public HashSet<Assembly> HandlerAssemblies { get; } = [];

    public bool ImplicitHandlerAutoRegistrationEnabled { get; set; } = true;

    public Dictionary<string, ZLinkChannelRegistration> Channels { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkStreamNodeRegistration> StreamNodes { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotNodeRegistration> SpotNodes { get; } = new(StringComparer.Ordinal);

    public ZLinkActorCatalog ActorCatalog { get; } = new();

    public TimeSpan ResolveChannelRequestTimeout(string channelName)
    {
        return Channels.TryGetValue(channelName, out var channel)
            ? channel.DefaultRequestTimeout ?? DefaultRequestTimeout
            : DefaultRequestTimeout;
    }

    public TimeSpan ResolveMeshRequestTimeout(string meshName)
    {
        return SpotNodes.TryGetValue(meshName, out var node)
            ? node.DefaultRequestTimeout ?? DefaultRequestTimeout
            : DefaultRequestTimeout;
    }

    public IEnumerable<Assembly> EnumerateHandlerScanAssemblies()
    {
        var assemblies = new HashSet<Assembly>(HandlerAssemblies);
        if (!ImplicitHandlerAutoRegistrationEnabled) return assemblies;

        foreach (var filterType in Filters) assemblies.Add(filterType.Assembly);

        foreach (var stream in StreamNodes.Values)
            if (stream.HeaderSessionType is not null)
                assemblies.Add(stream.HeaderSessionType.Assembly);

        foreach (var spotNode in SpotNodes.Values)
        {
            if (spotNode.EntrySpotType is not null) assemblies.Add(spotNode.EntrySpotType.Assembly);

            foreach (var spotType in spotNode.SpotFactories) assemblies.Add(spotType.Assembly);

            foreach (var instanceSpot in spotNode.InstanceSpotFactories.Values)
                assemblies.Add(instanceSpot.SpotType.Assembly);

            foreach (var actorFactoryType in spotNode.ActorFactories.Values) assemblies.Add(actorFactoryType.Assembly);

            foreach (var transfer in spotNode.ActorTransfers.Values)
            {
                assemblies.Add(transfer.ActorType.Assembly);
                assemblies.Add(transfer.AdapterType.Assembly);
            }

            foreach (var handler in spotNode.RouteSendHandlers) assemblies.Add(handler.HandlerType.Assembly);

            foreach (var handler in spotNode.RouteRequestHandlers) assemblies.Add(handler.HandlerType.Assembly);

            foreach (var membership in spotNode.ChannelMemberships)
            {
                foreach (var handler in membership.SendHandlers) assemblies.Add(handler.HandlerType.Assembly);

                foreach (var handler in membership.RequestHandlers) assemblies.Add(handler.HandlerType.Assembly);
            }
        }

        foreach (var channel in Channels.Values)
        {
            foreach (var handler in channel.SendHandlers) assemblies.Add(handler.HandlerType.Assembly);

            foreach (var handler in channel.RequestHandlers) assemblies.Add(handler.HandlerType.Assembly);

            foreach (var handler in channel.PublishHandlers) assemblies.Add(handler.HandlerType.Assembly);
        }

        return assemblies;
    }

    public ZLinkScannedHandlerCatalog ScannedHandlerCatalog =>
        _scannedHandlerCatalog ??= ZLinkScannedHandlerCatalog.Build(
            EnumerateHandlerScanAssemblies(),
            EnumerateSessionTypes());

    public void FreezeScannedHandlerCatalog()
    {
        _scannedHandlerCatalog = ZLinkScannedHandlerCatalog.Build(
            EnumerateHandlerScanAssemblies(),
            EnumerateSessionTypes());
    }

    private IReadOnlySet<Type> EnumerateSessionTypes() => StreamNodes.Values
        .Select(static node => node.HeaderSessionType)
        .Where(static type => type is not null)
        .Cast<Type>()
        .ToHashSet();
}

internal sealed record ZLinkScannedHandlerCatalog(
    IReadOnlyList<ZLinkHandlerEndpointDescriptor> ChannelEndpoints,
    IReadOnlyList<ZLinkScannedSpotHandler> SpotHandlers,
    IReadOnlyList<ZLinkScannedSessionHandler> SessionHandlers)
{
    public static ZLinkScannedHandlerCatalog Build(
        IEnumerable<Assembly> assemblies,
        IReadOnlySet<Type> sessionTypes)
    {
        var channelEndpoints = new List<ZLinkHandlerEndpointDescriptor>();
        var spotHandlers = new List<ZLinkScannedSpotHandler>();
        var sessionHandlers = new List<ZLinkScannedSessionHandler>();
        foreach (var assembly in assemblies)
        {
            channelEndpoints.AddRange(ZLinkHandlerScanner.Scan(assembly));
            spotHandlers.AddRange(ZLinkScannedSpotHandlerScanner.Scan(assembly));
            sessionHandlers.AddRange(ZLinkScannedSessionHandlerScanner.Scan(assembly, sessionTypes));
        }

        return new ZLinkScannedHandlerCatalog(
            Array.AsReadOnly(channelEndpoints.ToArray()),
            Array.AsReadOnly(spotHandlers.ToArray()),
            Array.AsReadOnly(sessionHandlers.ToArray()));
    }
}

internal sealed class ZLinkMetadataPolicyRegistration
{
    public HashSet<string> SessionToActorKeys { get; } = new(StringComparer.Ordinal);

    public HashSet<string> ActorToSessionKeys { get; } = new(StringComparer.Ordinal);
}

// Mesh channel marker: AddRouteMesh(meshName) registers the mesh discovery
// name and the MeshNode references it through SpotMeshChannelName. Peer
// acquisition is owned by location-store auto-connect or manual wiring.
internal sealed class ZLinkChannelRegistration
{
    public required string ChannelName { get; init; }

    public TimeSpan? DefaultRequestTimeout { get; set; }

    public ZLinkLocationAutoConnectType AutoConnectType { get; set; }

    public ZLinkChannelPublisherCapabilityRegistration? Publisher { get; set; }

    public ZLinkChannelSubscriberCapabilityRegistration? Subscriber { get; set; }

    public RoutingId RoutingId { get; set; }

    public bool HasExplicitRoutingId { get; set; }

    public ZLinkRoutingIdAllocationRegistration? RoutingIdAllocation { get; set; }

    public HashSet<string> HandlerGroups { get; } = new(StringComparer.Ordinal);

    public List<ZLinkChannelHandlerRegistration> SendHandlers { get; } = [];

    public List<ZLinkChannelHandlerRegistration> RequestHandlers { get; } = [];

    public List<ZLinkChannelHandlerRegistration> PublishHandlers { get; } = [];
}

internal sealed class ZLinkChannelPublisherCapabilityRegistration
{
    public string? BindEndpoint { get; set; }

    public ZLinkSocketConfig SocketConfig { get; } = new();
}

internal sealed class ZLinkChannelSubscriberCapabilityRegistration
{
    public ZLinkPeerAcquisitionMode AcquisitionMode { get; set; } = ZLinkPeerAcquisitionMode.Manual;

    public ZLinkSocketConfig SocketConfig { get; } = new();

    public ZLinkEndpointConnections ManualConnections { get; } = new();
}

internal sealed class ZLinkStreamNodeRegistration
{
    public required string StreamNodeName { get; init; }

    public string? BindEndpoint { get; set; }

    // MeshName pinned by EnableActorDispatch(meshName); null means this STREAM node
    // does not dispatch session Actors (spec 31 §2). Session resolve/bind/dispatch
    // is keyed on this MeshName, never inferred from "the sole spot node".
    public string? ActorDispatchMeshName { get; set; }

    public ZLinkStreamTlsServerRegistration? TlsServer { get; set; }

    public Type? HeaderSessionType { get; set; }
}

internal sealed record ZLinkStreamTlsServerRegistration(
    string CertPath,
    string KeyPath,
    bool RequireClientCert);

internal sealed record ZLinkRouteHandlerRegistration(
    Type HandlerType,
    Type MessageType,
    Type? ReplyType,
    string? PacketName);

internal sealed record ZLinkChannelHandlerRegistration(
    Type HandlerType,
    Type MessageType,
    Type? ReplyType,
    string? PacketName);

// One logical channel membership on a MeshNode (spec 05-route-mesh §4). The
// membership is (MeshName, ChannelName) scoped; weight is 0..100 (0 excludes the
// channel from new select-one/multicast targeting). Handlers are the channel's
// IZLinkSendHandler/IZLinkRequestHandler namespace.
internal sealed class ZLinkMeshChannelMembership
{
    public required string ChannelName { get; init; }

    private int _weight = ZLinkSocketConfig.DefaultPeerWeight;

    public int Weight
    {
        get => _weight;
        set
        {
            ZLinkSocketConfig.ValidatePeerWeight(value);
            _weight = value;
        }
    }

    public List<ZLinkChannelHandlerRegistration> SendHandlers { get; } = [];

    public List<ZLinkChannelHandlerRegistration> RequestHandlers { get; } = [];

    public HashSet<string> HandlerGroups { get; } = new(StringComparer.Ordinal);
}

internal sealed class ZLinkSpotNodeRegistration
{
    public required string SpotNodeName { get; init; }

    public string? SpotMeshChannelName { get; set; }

    public ZLinkSpotRouterCapabilityRegistration? Router { get; set; }

    // MeshNode-level default for RID-direct route requests handled by this node
    // (spec 05-route-mesh §2 IZLinkMeshNodeBuilder.SetDefaultRequestTimeout). Null
    // falls back to the framework-wide DefaultRequestTimeout.
    public TimeSpan? DefaultRequestTimeout { get; set; }

    // Logical Multicast publisher transport settings for this MeshNode
    // (spec §5 IZLinkSpotPublisherConfig, ConfigureSpotPublisher).
    public ZLinkSpotPublisherConfig SpotPublisherConfig { get; } = new();

    // Immutable logical channel memberships added via ChannelName(...) (spec §4).
    // Each carries its build-time weight and channel-scoped handler namespace.
    public List<ZLinkMeshChannelMembership> ChannelMemberships { get; } = [];

    // RID-direct route handlers registered on the MeshNode builder itself
    // (spec §2 AddRouteSendHandler/AddRouteRequestHandler). They share the node
    // ROUTER's source-RID route-handler context.
    public List<ZLinkRouteHandlerRegistration> RouteSendHandlers { get; } = [];

    public List<ZLinkRouteHandlerRegistration> RouteRequestHandlers { get; } = [];

    public HashSet<Type> SpotFactories { get; } = [];

    public Dictionary<string, ZLinkInstanceSpotFactoryRegistration>
        InstanceSpotFactories { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, Type> ActorFactories { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkActorTransferRegistration> ActorTransfers { get; } = new(StringComparer.Ordinal);

    public RoutingId RoutingId { get; set; }

    public bool HasExplicitRoutingId { get; set; }

    public ZLinkRoutingIdAllocationRegistration? RoutingIdAllocation { get; set; }

    public ZLinkEntrySpotOptions EntrySpotOptions { get; } = new();

    public bool HasExplicitEntrySpotRoutingId { get; set; }

    public Type? EntrySpotType { get; set; }
}

internal sealed class ZLinkRoutingIdAllocationRegistration
{
    public required int SlotCount { get; init; }

    public required string RoutingIdPrefix { get; init; }

    public string? GroupName { get; set; }
}

internal sealed record ZLinkActorTransferRegistration(
    Type ActorType,
    Type AdapterType,
    IZLinkActorTransferInvoker Invoker);

internal sealed record ZLinkInstanceSpotFactoryRegistration(
    Type SpotType,
    ZLinkInstanceSpotFactoryOptions Options);

internal sealed class ZLinkActorCatalog
{
    private IReadOnlyDictionary<string, Type> _factories = FrozenDictionary<string, Type>.Empty;
    private IReadOnlyDictionary<string, ZLinkActorTransferRegistration> _transfers =
        FrozenDictionary<string, ZLinkActorTransferRegistration>.Empty;

    public IReadOnlyDictionary<string, Type> Factories => _factories;

    public IReadOnlyDictionary<string, ZLinkActorTransferRegistration> Transfers => _transfers;

    public void Build(IEnumerable<ZLinkSpotNodeRegistration> spotNodes)
    {
        var factories = new Dictionary<string, Type>(StringComparer.Ordinal);
        var transfers = new Dictionary<string, ZLinkActorTransferRegistration>(StringComparer.Ordinal);
        foreach (var spotNode in spotNodes)
        {
            foreach (var (actorType, factoryType) in spotNode.ActorFactories)
                factories.TryAdd(actorType, factoryType);

            foreach (var (actorType, transfer) in spotNode.ActorTransfers)
                if (!transfers.TryAdd(actorType, transfer))
                    throw new ZLinkConfigurationException(
                        $"Duplicate actor transfer '{actorType}' across SpotNodes.");
        }

        _factories = factories.ToFrozenDictionary(StringComparer.Ordinal);
        _transfers = transfers.ToFrozenDictionary(StringComparer.Ordinal);
    }

    public bool TryGetTransfer(
        string actorType,
        out ZLinkActorTransferRegistration? transfer)
    {
        return _transfers.TryGetValue(actorType, out transfer);
    }

    public Type ResolveFactory(string actorType)
    {
        return _factories.TryGetValue(actorType, out var factoryType)
            ? factoryType
            : throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorCreateFailed,
                $"Actor factory '{actorType}' is not registered.");
    }
}

internal sealed class ZLinkEntrySpotOptions : IZLinkEntrySpotOptions
{
    public RoutingId RoutingId { get; set; }
}

internal sealed class ZLinkSpotRouterCapabilityRegistration
{
    public ZLinkPeerAcquisitionMode AcquisitionMode { get; set; } = ZLinkPeerAcquisitionMode.Manual;

    public string? BindEndpoint { get; set; }

    public ZLinkSocketConfig SocketConfig { get; } = new();

    public ZLinkRouteConfig RoutingConfig { get; } = new();

    public ZLinkEndpointConnections ManualConnections { get; } = new();

    public Dictionary<string, RoutingId> PeerRoutingIds { get; } = new(StringComparer.Ordinal);
}
