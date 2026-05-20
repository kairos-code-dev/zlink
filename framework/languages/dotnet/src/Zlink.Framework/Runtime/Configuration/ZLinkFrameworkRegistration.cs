using System.Reflection;

namespace Zlink.Framework.Runtime.Configuration;

internal sealed class ZLinkFrameworkRegistration
{
    public TimeSpan DefaultTimeout { get; set; } = TimeSpan.FromSeconds(30);

    public ZLinkCodecRegistryBuilder Codecs { get; } = new();

    public ZLinkMetadataPolicyRegistration MetadataPolicy { get; } = new();

    public ZLinkDispatchOptionsModel DispatchOptions { get; } = new();

    public List<Type> Filters { get; } = [];

    public HashSet<Assembly> HandlerAssemblies { get; } = [];

    public Dictionary<string, Type> ActorFactories { get; } = new(StringComparer.Ordinal);

    public Type? ActorPlayRouteResolverType { get; set; }

    public Type? SpotRouteResolverType { get; set; }

    public Type? ActorSessionBindingStoreType { get; set; }

    public ZLinkRegistryActorRoutesRegistration? RegistryActorRoutes { get; set; }

    public ZLinkRegistrySpotRoutesRegistration? RegistrySpotRoutes { get; set; }

    public Dictionary<string, ZLinkChannelRegistration> Channels { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkRouteChannelRegistration> RouteChannels { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkStreamNodeRegistration> StreamNodes { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotNodeRegistration> SpotNodes { get; } = new(StringComparer.Ordinal);

    public ZLinkDiscoveryRegistration? Discovery { get; set; }

    public ZLinkSpotDiscoveryRegistration? SpotDiscovery { get; set; }
}

internal sealed class ZLinkRegistryActorRoutesRegistration
{
    public required string Namespace { get; init; }

    public string? RouterChannelId { get; set; }
}

internal sealed class ZLinkRegistrySpotRoutesRegistration
{
    public required string Namespace { get; init; }

    public string? RouterChannelId { get; set; }
}

internal class ZLinkDiscoveryRegistration
{
    public List<string> Endpoints { get; } = [];
}

internal sealed class ZLinkMetadataPolicyRegistration
{
    public HashSet<string> ForwardedApplicationKeys { get; } = new(StringComparer.Ordinal);
}

internal sealed class ZLinkSpotDiscoveryRegistration : ZLinkDiscoveryRegistration
{
    public required string ChannelName { get; init; }

    public bool RequiresUseDiscovery { get; init; }

    public bool UseDiscoveryCalled { get; set; }
}

internal sealed class ZLinkChannelRegistration
{
    public required string ChannelName { get; init; }

    public ZLinkAutoConnectType AutoConnectType { get; set; }

    public ZLinkChannelServerCapabilityRegistration? Server { get; set; }

    public ZLinkChannelClientCapabilityRegistration? Client { get; set; }

    public ZLinkChannelPublisherCapabilityRegistration? Publisher { get; set; }

    public ZLinkChannelSubscriberCapabilityRegistration? Subscriber { get; set; }

    public HashSet<string> HandlerGroups { get; } = new(StringComparer.Ordinal);
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

    public Type? HeaderSessionType { get; set; }
}

internal sealed class ZLinkRouteChannelRegistration
{
    public required string RouterChannelId { get; init; }

    public string? BindEndpoint { get; set; }

    public ZLinkSocketConfig SocketConfig { get; } = new();

    public ZLinkRouteConfig RoutingConfig { get; } = new();

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

internal sealed class ZLinkSpotNodeRegistration
{
    public required string SpotNodeName { get; init; }

    public string? BindEndpoint { get; set; }

    public ZLinkSpotRouterCapabilityRegistration? Router { get; set; }

    public ZLinkSpotPubSubCapabilityRegistration? PubSub { get; set; }

    public Dictionary<string, ZLinkSpotChannelClientRegistration> AttachedChannelClients { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotPublisherClientRegistration> AttachedSpotPublisherClients { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, Type> SpotFactories { get; } = new(StringComparer.Ordinal);

    public Type? EntrySpotType { get; set; }
}

internal sealed class ZLinkSpotRouterCapabilityRegistration
{
    public ZLinkSocketConfig SocketConfig { get; } = new();

    public ZLinkRouteConfig RoutingConfig { get; } = new();

    public List<string> ManualConnections { get; } = [];
}

internal sealed class ZLinkSpotPubSubCapabilityRegistration
{
    public ZLinkSpotPublisherConfig PublisherConfig { get; } = new();

    public ZLinkSpotSubscriberConfig SubscriberConfig { get; } = new();

    public List<string> ManualConnections { get; } = [];
}

internal sealed class ZLinkSpotChannelClientRegistration
{
    public required string ChannelName { get; init; }

    public ZLinkSocketConfig SocketConfig { get; } = new();

    public ZLinkOutboundRouteConfig RoutingConfig { get; } = new();

    public List<string> ManualConnections { get; } = [];
}

internal sealed class ZLinkSpotPublisherClientRegistration
{
    public required string ChannelName { get; init; }

    public ZLinkSocketConfig SocketConfig { get; } = new();

    public List<string> ManualConnections { get; } = [];
}
