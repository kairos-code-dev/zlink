using System.Reflection;

namespace Zlink.Framework.Runtime.Configuration;

internal sealed class ZLinkFrameworkRegistration
{
    public TimeSpan DefaultRequestTimeout { get; set; } = TimeSpan.FromSeconds(30);

    public TimeSpan? DefaultSocketSendTimeout { get; set; } = TimeSpan.FromMilliseconds(1000);

    public ZLinkCodecRegistryBuilder Codecs { get; } = new();

    public IZlinkStreamCompressionCodec? StreamCompressionCodec { get; set; } =
        ZLinkStreamProtocolDefaults.CreateLz4CompressionCodec();

    public ZLinkMetadataPolicyRegistration MetadataPolicy { get; } = new();

    public ZLinkDispatchOptionsModel DispatchOptions { get; } = new();

    public ZLinkWorkerOptionsModel WorkerOptions { get; } = new();

    public List<Type> Filters { get; } = [];

    public ZLinkLocationRegistration Locations { get; } = new();

    public HashSet<Assembly> HandlerAssemblies { get; } = [];

    public Type? SpotRouteRefResolverType { get; set; }

    public Dictionary<string, ZLinkChannelRegistration> Channels { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkRouteChannelRegistration> RouteChannels { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkStreamNodeRegistration> StreamNodes { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotNodeRegistration> SpotNodes { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotMeshChannelRegistration> SpotMeshChannels { get; } = new(StringComparer.Ordinal);

    public ZLinkSpotMeshChannelRegistration? SpotDiscovery
        => SpotMeshChannels.Count == 1 ? SpotMeshChannels.Values.Single() : null;

    public TimeSpan ResolveChannelRequestTimeout(string channelName)
    {
        return Channels.TryGetValue(channelName, out var channel)
            ? channel.DefaultRequestTimeout ?? DefaultRequestTimeout
            : DefaultRequestTimeout;
    }

    public TimeSpan ResolveRouteRequestTimeout(string routerChannelId)
    {
        return RouteChannels.TryGetValue(routerChannelId, out var channel)
            ? channel.DefaultRequestTimeout ?? DefaultRequestTimeout
            : DefaultRequestTimeout;
    }
}

internal sealed class ZLinkMetadataPolicyRegistration
{
    public HashSet<string> ForwardedApplicationKeys { get; } = new(StringComparer.Ordinal);
}

// spot mesh channel marker: AddSpotMesh(...) registers the mesh channel
// name and the spot node references it through SpotMeshChannelName. Peer
// acquisition is owned by location-store auto-connect or manual wiring.
internal sealed class ZLinkSpotMeshChannelRegistration
{
    public required string ChannelName { get; init; }
}

internal sealed class ZLinkChannelRegistration
{
    public required string ChannelName { get; init; }

    public TimeSpan? DefaultRequestTimeout { get; set; }

    public ZLinkAutoConnectType AutoConnectType { get; set; }

    public ZLinkChannelServerCapabilityRegistration? Server { get; set; }

    public ZLinkChannelClientCapabilityRegistration? Client { get; set; }

    public ZLinkChannelPublisherCapabilityRegistration? Publisher { get; set; }

    public ZLinkChannelSubscriberCapabilityRegistration? Subscriber { get; set; }

    public RoutingId RoutingId { get; set; }

    public HashSet<string> HandlerGroups { get; } = new(StringComparer.Ordinal);

    public List<ZLinkChannelHandlerRegistration> SendHandlers { get; } = [];

    public List<ZLinkChannelHandlerRegistration> RequestHandlers { get; } = [];

    public List<ZLinkChannelHandlerRegistration> PublishHandlers { get; } = [];
}

internal sealed class ZLinkChannelServerCapabilityRegistration
{
    public string? BindEndpoint { get; set; }

    public ZLinkSocketConfig SocketConfig { get; } = new();

    public ZLinkRouteConfig RoutingConfig { get; } = new();
}

internal sealed class ZLinkChannelClientCapabilityRegistration
{
    public string? BindEndpoint { get; set; }

    public ZLinkSocketConfig SocketConfig { get; } = new();

    public ZLinkOutboundRouteConfig RoutingConfig { get; } = new();

    public List<string> ManualConnections { get; } = [];
}

internal sealed class ZLinkChannelPublisherCapabilityRegistration
{
    public string? BindEndpoint { get; set; }

    public ZLinkSocketConfig SocketConfig { get; } = new();
}

internal sealed class ZLinkChannelSubscriberCapabilityRegistration
{
    public ZLinkSocketConfig SocketConfig { get; } = new();

    public List<string> ManualConnections { get; } = [];
}

internal sealed class ZLinkStreamNodeRegistration
{
    public required string StreamNodeName { get; init; }

    public string? BindEndpoint { get; set; }

    public ZLinkStreamTlsServerRegistration? TlsServer { get; set; }

    public Type? HeaderSessionType { get; set; }
}

internal sealed record ZLinkStreamTlsServerRegistration(
    string CertPath,
    string KeyPath,
    bool RequireClientCert);

internal sealed class ZLinkRouteChannelRegistration
{
    public required string RouterChannelId { get; init; }

    public TimeSpan? DefaultRequestTimeout { get; set; }

    public string? BindEndpoint { get; set; }

    public bool ClientEnabled { get; set; }

    public ZLinkSocketConfig SocketConfig { get; } = new();

    public ZLinkRouteConfig RoutingConfig { get; } = new();

    public RoutingId RoutingId { get; set; }

    public List<string> ManualConnections { get; } = [];

    public List<ZLinkRouteHandlerRegistration> SendHandlers { get; } = [];

    public List<ZLinkRouteHandlerRegistration> RequestHandlers { get; } = [];

    public HashSet<string> HandlerGroups { get; } = new(StringComparer.Ordinal);
}

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

internal sealed class ZLinkSpotNodeRegistration
{
    public required string SpotNodeName { get; init; }

    public string? SpotMeshChannelName { get; set; }

    public ZLinkSpotRouterCapabilityRegistration? Router { get; set; }

    public ZLinkSpotPubSubCapabilityRegistration? PubSub { get; set; }

    public HashSet<Type> SpotFactories { get; } = [];

    public Dictionary<string, Type> ActorFactories { get; } = new(StringComparer.Ordinal);

    public RoutingId RoutingId { get; set; }

    public ZLinkEntrySpotOptions EntrySpotOptions { get; } = new();

    public Type? EntrySpotType { get; set; }
}

internal sealed class ZLinkEntrySpotOptions : IZLinkEntrySpotOptions
{
    public RoutingId RoutingId { get; set; }
}

internal sealed class ZLinkSpotRouterCapabilityRegistration
{
    public string? BindEndpoint { get; set; }

    public ZLinkSocketConfig SocketConfig { get; } = new();

    public ZLinkRouteConfig RoutingConfig { get; } = new();

    public List<ZLinkSpotRouterManualConnectionRegistration> ManualConnections { get; } = [];
}

internal sealed record ZLinkSpotRouterManualConnectionRegistration(
    string Endpoint,
    RoutingId? PeerRid);

internal sealed class ZLinkSpotPubSubCapabilityRegistration
{
    public string? BindEndpoint { get; set; }

    public ZLinkSpotPublisherConfig PublisherConfig { get; } = new();

    public ZLinkSpotSubscriberConfig SubscriberConfig { get; } = new();

    public List<string> ManualConnections { get; } = [];
}
