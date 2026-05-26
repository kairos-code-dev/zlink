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

    public Type? SpotRemoteAddressResolverType { get; set; }

    public ZLinkRegistrySpotRemoteAddressesRegistration? RegistrySpotRemoteAddresses { get; set; }

    public Dictionary<string, ZLinkChannelRegistration> Channels { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkRouteChannelRegistration> RouteChannels { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkStreamNodeRegistration> StreamNodes { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotNodeRegistration> SpotNodes { get; } = new(StringComparer.Ordinal);

    public ZLinkDiscoveryRegistration? Discovery { get; set; }

    public ZLinkSpotDiscoveryRegistration? SpotDiscovery { get; set; }
}

internal sealed class ZLinkRegistrySpotRemoteAddressesRegistration
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

    public List<ZLinkChannelHandlerRegistration> SendHandlers { get; } = [];

    public List<ZLinkChannelHandlerRegistration> RequestHandlers { get; } = [];

    public List<ZLinkChannelHandlerRegistration> PublishHandlers { get; } = [];

    public ZLinkSpotRouteEgressRegistration? SpotRouteEgress { get; set; }
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

    public string? ActorGatewaySpotNodeName { get; set; }

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

    public ZLinkSpotRouteEgressRegistration? SpotRouteEgress { get; set; }

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

internal sealed record ZLinkSpotRouteEgressRegistration(
    string TargetSpotNodeChannelName);

internal sealed class ZLinkSpotNodeRegistration
{
    public required string SpotNodeName { get; init; }

    public ZLinkSpotRouterCapabilityRegistration? Router { get; set; }

    public ZLinkSpotPubSubCapabilityRegistration? PubSub { get; set; }

    public Dictionary<string, ZLinkSpotChannelClientRegistration> AttachedChannelClients { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotPublisherClientRegistration> AttachedSpotPublisherClients { get; } = new(StringComparer.Ordinal);

    public Dictionary<string, ZLinkSpotRouteChannelAcceptanceRegistration> AcceptedSpotRouteChannels { get; } = new(StringComparer.Ordinal);

    public HashSet<Type> SpotFactories { get; } = [];

    public ZLinkEntrySpotOptions EntrySpotOptions { get; } = new();

    public Type? EntrySpotType { get; set; }
}

internal sealed class ZLinkEntrySpotOptions : IZLinkEntrySpotOptions
{
    public RoutingId RoutingId { get; set; }
}

internal sealed class ZLinkSpotRouteChannelAcceptanceRegistration
{
    public required string ChannelName { get; init; }

    public List<string> ManualConnections { get; } = [];
}

internal sealed class ZLinkSpotRouterCapabilityRegistration
{
    public string? BindEndpoint { get; set; }

    public ZLinkSocketConfig SocketConfig { get; } = new();

    public ZLinkRouteConfig RoutingConfig { get; } = new();

    public List<string> ManualConnections { get; } = [];
}

internal sealed class ZLinkSpotPubSubCapabilityRegistration
{
    public string? BindEndpoint { get; set; }

    public RoutingId RoutingId { get; set; }

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
