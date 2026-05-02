namespace Zlink.Framework.Configuration;

public interface IZLinkDiscoveryBuilder
{
    void Add(string endpoint);
}

public interface IChannelServerCapabilityBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(Action<IRoutedPeerOptions> configure);
}

public interface IChannelClientCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(Action<IOutboundPeerOptions> configure);

    void UseManualConnections(Action<IChannelClientConnections> configure);
}

public interface IDealerMeshChannelClientCapabilityBuilder : IChannelClientCapabilityBuilder
{
    void Bind(string endpoint);
}

public interface IChannelPublisherCapabilityBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);
}

public interface IChannelSubscriberCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void UseManualConnections(Action<IChannelSubscriberConnections> configure);
}

public interface ISpotRouterCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(Action<IRoutedPeerOptions> configure);

    void UseManualConnections(Action<ISpotRouterConnections> configure);
}

public interface ISpotPubSubCapabilityBuilder
{
    void ConfigurePublisherOptions(Action<ISpotNodePublisherOptions> configure);

    void ConfigureSubscriberOptions(Action<ISpotNodeSubscriberOptions> configure);

    void UseManualConnections(Action<ISpotPubSubConnections> configure);
}

public interface ISpotPublisherClientCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void UseManualConnections(Action<ISpotPublisherConnections> configure);
}

public interface ISpotChannelClientCapabilityBuilder
{
    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(Action<IOutboundPeerOptions> configure);

    void UseManualConnections(Action<IChannelClientConnections> configure);
}

public interface IRoutedChannelConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface IZLinkRoutedChannelBuilder
{
    void Bind(string endpoint);

    void ConfigureSocket(Action<IZLinkCommonSocketOptions> configure);

    void ConfigureRouting(Action<IRoutedPeerOptions> configure);

    void UseManualConnections(Action<IRoutedChannelConnections> configure);

    void AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRoutedSendHandler<TMessage>;

    void AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRoutedRequestHandler<TRequest, TReply>;
}

public interface IZLinkRouteMeshChannelBuilder : IZLinkRoutedChannelBuilder
{
}

public interface IZLinkStreamNodeBuilder
{
    void Bind(string endpoint);

    void AddHeaderSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkChannelBuilder
{
    void EnableServer(Action<IChannelServerCapabilityBuilder>? configure = null);

    void EnableClient(Action<IChannelClientCapabilityBuilder>? configure = null);

    void EnablePublisher(Action<IChannelPublisherCapabilityBuilder>? configure = null);

    void EnableSubscriber(Action<IChannelSubscriberCapabilityBuilder>? configure = null);
}

public interface IZLinkClientServerChannelBuilder
{
    void EnableServer(Action<IChannelServerCapabilityBuilder>? configure = null);

    void EnableClient(Action<IChannelClientCapabilityBuilder>? configure = null);
}

public interface IZLinkFanoutChannelBuilder
{
    void EnablePublisher(Action<IChannelPublisherCapabilityBuilder>? configure = null);

    void EnableSubscriber(Action<IChannelSubscriberCapabilityBuilder>? configure = null);
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

    void AddSpotFactory<TSpot>(string spotName)
        where TSpot : IZLinkSpot;
}

public interface IZLinkSpotMeshNodeBuilder
{
    void Bind(string endpoint);

    void EnableRouter(Action<ISpotRouterCapabilityBuilder>? configure = null);

    void EnablePubSub(Action<ISpotPubSubCapabilityBuilder>? configure = null);

    void AttachClientServerChannelClient(
        string channelName,
        Action<ISpotChannelClientCapabilityBuilder>? configure = null);

    void AttachSpotMeshPublisherClient(
        string channelName,
        Action<ISpotPublisherClientCapabilityBuilder>? configure = null);

    void AddSpotFactory<TSpot>(string spotName)
        where TSpot : IZLinkSpot;
}

public interface IZLinkSpotMeshBuilder
{
    void UseDiscovery(Action<IZLinkDiscoveryBuilder> configure);

    void AddNode(
        string spotNodeName,
        Action<IZLinkSpotMeshNodeBuilder> configure);
}

public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultTimeout { get; set; }

    IZLinkCodecRegistryBuilder Codecs { get; }

    void ConfigureMetadata(Action<IZLinkMetadataPolicyBuilder> configure);

    void AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;

    void AddActorPlayRouteResolver<TResolver>()
        where TResolver : class, IZLinkActorPlayRouteResolver;

    void AddActorSessionRouteResolver<TResolver>()
        where TResolver : class, IZLinkActorSessionRouteResolver;

    void AddActorSessionLocationWriter<TWriter>()
        where TWriter : class, IZLinkActorSessionLocationWriter;

    void AddChannel(
        string channelName,
        Action<IZLinkChannelBuilder> configure);

    void AddClientServerChannel(
        string channelName,
        Action<IZLinkClientServerChannelBuilder> configure);

    void AddFanoutChannel(
        string channelName,
        Action<IZLinkFanoutChannelBuilder> configure);

    void AddDealerMeshChannel(
        string channelName,
        Action<IZLinkDealerMeshChannelBuilder> configure);

    void AddRoutedChannel(
        string routerChannelId,
        Action<IZLinkRoutedChannelBuilder> configure);

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
