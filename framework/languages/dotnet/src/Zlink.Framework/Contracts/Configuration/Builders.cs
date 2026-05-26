namespace Zlink.Framework.Contracts.Configuration;

public interface IZLinkDiscoveryBuilder
{
    void Add(string endpoint);
}

public interface IChannelServerCapabilityBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(Action<IZLinkSocketConfig> configure);

    void ConfigureRouting(Action<IZLinkRouteConfig> configure);
}

public interface IChannelClientCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkSocketConfig> configure);

    void ConfigureRouting(Action<IZLinkOutboundRouteConfig> configure);

    void UseManualConnections(Action<IChannelClientConnections> configure);
}

public interface IDealerMeshChannelClientCapabilityBuilder : IChannelClientCapabilityBuilder
{
    void Bind(string endpoint);
}

public interface IChannelPublisherCapabilityBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(Action<IZLinkSocketConfig> configure);
}

public interface IChannelSubscriberCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkSocketConfig> configure);

    void UseManualConnections(Action<IChannelSubscriberConnections> configure);
}

public interface ISpotRouterCapabilityBuilder
{
    void SetRouterBind(string endpoint);

    void SetRoutingId(RoutingId routingId);

    void ConfigureSocket(Action<IZLinkSocketConfig> configure);

    void ConfigureRouting(Action<IZLinkRouteConfig> configure);

    void UseManualConnections(Action<ISpotRouterConnections> configure);
}

public interface ISpotPubSubCapabilityBuilder
{
    void SetPubBind(string endpoint);

    void SetRoutingId(RoutingId routingId);

    void ConfigurePublisherConfig(Action<IZLinkSpotPublisherConfig> configure);

    void ConfigureSubscriberConfig(Action<IZLinkSpotSubscriberConfig> configure);

    void UseManualConnections(Action<ISpotPubSubConnections> configure);
}

public interface ISpotPublisherClientCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkSocketConfig> configure);

    void UseManualConnections(Action<ISpotPublisherConnections> configure);
}

public interface ISpotChannelClientCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkSocketConfig> configure);

    void ConfigureRouting(Action<IZLinkOutboundRouteConfig> configure);

    void UseManualConnections(Action<IChannelClientConnections> configure);
}

public interface IRouteChannelConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface IZLinkRouteChannelBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(Action<IZLinkSocketConfig> configure);

    void ConfigureRouting(Action<IZLinkRouteConfig> configure);

    void UseManualConnections(Action<IRouteChannelConnections> configure);

    void AddHandlerGroup(string groupName);

    void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>;

    void AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>;

    void AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;

    void EnableSpotRouteEgress(string targetSpotNodeChannelName);
}

public interface IZLinkRouteMeshChannelBuilder : IZLinkRouteChannelBuilder
{
}

public interface IZLinkStreamNodeBuilder
{
    void Bind(string endpoint);

    void AttachActorGateway(string spotNodeName);

    void RegisterSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkClientServerChannelBuilder
{
    void EnableServer(Action<IChannelServerCapabilityBuilder>? configure = null);

    void EnableClient(Action<IChannelClientCapabilityBuilder>? configure = null);

    void AddHandlerGroup(string groupName);

    void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;

    void AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;

    void AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;

    void EnableSpotRouteEgress(string targetSpotNodeChannelName);
}

public interface IZLinkFanoutChannelBuilder
{
    void EnablePublisher(Action<IChannelPublisherCapabilityBuilder>? configure = null);

    void EnableSubscriber(Action<IChannelSubscriberCapabilityBuilder>? configure = null);

    void AddHandlerGroup(string groupName);

    void AddPublishHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkPublishHandler<TMessage>;

    void AddPublishHandler<THandler>(string? packetName = null)
        where THandler : class;
}

public interface IZLinkDealerMeshChannelBuilder
{
    void EnableClient(Action<IDealerMeshChannelClientCapabilityBuilder>? configure = null);

    void AddHandlerGroup(string groupName);

    void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;

    void AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;

    void AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;
}

public interface IZLinkSpotNodeBuilder
{
    void EnableRouter(Action<ISpotRouterCapabilityBuilder>? configure = null);

    void EnablePubSub(Action<ISpotPubSubCapabilityBuilder>? configure = null);

    void AttachChannelClient(
        string channelName,
        Action<ISpotChannelClientCapabilityBuilder>? configure = null);

    void AttachClientServerChannelClient(
        string channelName,
        Action<ISpotChannelClientCapabilityBuilder>? configure = null);

    void AttachSpotPublisherClient(
        string channelName,
        Action<ISpotPublisherClientCapabilityBuilder>? configure = null);

    void AttachSpotMeshPublisherClient(
        string channelName,
        Action<ISpotPublisherClientCapabilityBuilder>? configure = null);

    void AcceptSpotRoutesFromChannel(
        string channelName,
        Action<IZLinkSpotRouteChannelAcceptanceBuilder>? configure = null);

    void ConfigureEntrySpot(Action<IZLinkEntrySpotOptions> configure);

    void AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot;

    void AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot;
}

public interface IZLinkSpotMeshNodeBuilder : IZLinkSpotNodeBuilder
{
}

public interface IZLinkSpotRouteChannelAcceptanceBuilder
{
    void UseManualConnections(Action<ISpotRouterChannelConnections> configure);
}

public interface ISpotRouterChannelConnections
{
    void Connect(string endpoint);
}

public interface IZLinkSpotMeshBuilder
{
    void UseDiscovery(Action<IZLinkDiscoveryBuilder> configure);

    void AddNode(
        string spotNodeName,
        Action<IZLinkSpotMeshNodeBuilder> configure);
}

public interface IZLinkRegistrySpotRemoteAddressesOptions
{
    string? RouterChannelId { get; set; }
}

public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultTimeout { get; set; }

    IZLinkCodecRegistryBuilder Codecs { get; }

    void AddHandlersFromAssemblyOf<TMarker>();

    void AddHandlersFromAssemblyOf(Type markerType);

    void AddHandlersFromAssembly(System.Reflection.Assembly assembly);

    void ConfigureMetadata(Action<IZLinkMetadataPolicyBuilder> configure);

    void AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;

    void AddSpotRemoteAddressResolver<TResolver>()
        where TResolver : class, IZLinkSpotRemoteAddressResolver;

    void UseRegistrySpotRemoteAddresses(string namespaceName);

    void UseRegistrySpotRemoteAddresses(
        string namespaceName,
        Action<IZLinkRegistrySpotRemoteAddressesOptions> configure);

    void AddClientServerChannel(
        string channelName,
        Action<IZLinkClientServerChannelBuilder> configure);

    void AddFanoutChannel(
        string channelName,
        Action<IZLinkFanoutChannelBuilder> configure);

    void AddDealerMeshChannel(
        string channelName,
        Action<IZLinkDealerMeshChannelBuilder> configure);

    void AddRouteMeshChannel(
        string channelName,
        Action<IZLinkRouteMeshChannelBuilder> configure);

    void UseDiscovery(Action<IZLinkDiscoveryBuilder> configure);

    void UseFilter<TFilter>()
        where TFilter : class, IZLinkHandlerFilter;

    void ConfigureDispatch(Action<IZLinkDispatchOptions> configure);

    void AddStreamNode(
        string streamNodeName,
        Action<IZLinkStreamNodeBuilder> configure);

    void AddSpotMesh(
        string channelName,
        Action<IZLinkSpotMeshBuilder> configure);
}

public interface IZLinkMetadataPolicyBuilder
{
    void ForwardApplicationKey(string key);
}
