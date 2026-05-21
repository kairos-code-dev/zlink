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
    void ConfigureSocket(Action<IZLinkSocketConfig> configure);

    void ConfigureRouting(Action<IZLinkRouteConfig> configure);

    void UseManualConnections(Action<ISpotRouterConnections> configure);
}

public interface ISpotPubSubCapabilityBuilder
{
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

    void MapHandlerGroup(string groupName);

    void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>;

    void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>;
}

public interface IZLinkRouteMeshChannelBuilder : IZLinkRouteChannelBuilder
{
}

public interface IZLinkStreamNodeBuilder
{
    void Bind(string endpoint);

    void AddHeaderSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkClientServerChannelBuilder
{
    void EnableServer(Action<IChannelServerCapabilityBuilder>? configure = null);

    void EnableClient(Action<IChannelClientCapabilityBuilder>? configure = null);

    void MapHandlerGroup(string groupName);
}

public interface IZLinkFanoutChannelBuilder
{
    void EnablePublisher(Action<IChannelPublisherCapabilityBuilder>? configure = null);

    void EnableSubscriber(Action<IChannelSubscriberCapabilityBuilder>? configure = null);

    void MapHandlerGroup(string groupName);
}

public interface IZLinkDealerMeshChannelBuilder
{
    void EnableClient(Action<IDealerMeshChannelClientCapabilityBuilder>? configure = null);
}

public interface IZLinkSpotNodeBuilder
{
    void Bind(string endpoint);

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

    void AddSpotFactory<TSpot>(string spotName)
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

public interface IZLinkRegistryActorRoutesOptions
{
    string? RouterChannelId { get; set; }
}

public interface IZLinkRegistrySpotRoutesOptions
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

    void AddActorPlayRouteResolver<TResolver>()
        where TResolver : class, IZLinkActorPlayRouteResolver;

    void AddSpotRouteResolver<TResolver>()
        where TResolver : class, IZLinkSpotRouteResolver;

    void AddActorSessionBindingStore<TStore>()
        where TStore : class, IZLinkActorSessionBindingStore;

    void UseRegistryActorRoutes(string namespaceName);

    void UseRegistryActorRoutes(
        string namespaceName,
        Action<IZLinkRegistryActorRoutesOptions> configure);

    void UseRegistrySpotRoutes(string namespaceName);

    void UseRegistrySpotRoutes(
        string namespaceName,
        Action<IZLinkRegistrySpotRoutesOptions> configure);

    void UseRegistryActorSessionBindings(string namespaceName);

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

    void UseSpotDiscovery(
        string channelName,
        Action<IZLinkDiscoveryBuilder> configure);

    void UseFilter<TFilter>()
        where TFilter : class, IZLinkHandlerFilter;

    void ConfigureDispatch(Action<IZLinkDispatchOptions> configure);

    void AddStreamNode(
        string streamNodeName,
        Action<IZLinkStreamNodeBuilder> configure);

    void AddSpotNode(
        string spotNodeName,
        Action<IZLinkSpotNodeBuilder> configure);

    void AddSpotMesh(
        string channelName,
        Action<IZLinkSpotMeshBuilder> configure);
}

public interface IZLinkMetadataPolicyBuilder
{
    void ForwardApplicationKey(string key);
}
