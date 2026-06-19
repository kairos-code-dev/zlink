namespace Zlink.Framework.Contracts.Configuration;

public interface IZLinkDiscoveryBuilder
{
    IZLinkDiscoveryBuilder AddRegistryEndpoint(string endpoint);
}

public interface IZLinkRouteMeshChannelBuilder
{
    // route mesh 는 ROUTER ↔ ROUTER 대칭이라 server·client 가 같은 ROUTER 소켓을 공유한다.
    // EnableServer 는 이 노드가 bind 해서 route ingress 를 받는(제공) 쪽을,
    // EnableClient 는 다른 ROUTER 에 connect 해 outbound route 를 보내는(소비) 쪽을 설정한다.
    // 한 노드가 둘 다 켤 수 있다.
    IZLinkRouteMeshChannelBuilder EnableServer(string endpoint);

    IZLinkRouteMeshChannelBuilder EnableClient();

    IZLinkRouteMeshChannelBuilder EnableClient(string endpoint);

    IZLinkSocketConfig ConfigureSocket();

    IZLinkRouteConfig ConfigureRouting();

    IZLinkRouteMeshChannelBuilder AddHandlerGroup(string groupName);

    IZLinkRouteMeshChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>;

    IZLinkRouteMeshChannelBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkRouteMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>;

    IZLinkRouteMeshChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkRouteMeshChannelBuilder EnableSpotRouteEgress(string targetSpotNodeChannelName);
}

public interface IZLinkStreamNodeBuilder
{
    IZLinkStreamNodeBuilder Bind(string endpoint);

    IZLinkStreamNodeBuilder AttachActorGateway(string spotNodeName);

    IZLinkStreamNodeBuilder RegisterSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkClientServerChannelBuilder
{
    IZLinkClientServerChannelBuilder EnableServer(string endpoint);

    IZLinkClientServerChannelBuilder EnableClient();

    IZLinkClientServerChannelBuilder EnableClient(string endpoint);

    IZLinkClientServerChannelBuilder AddHandlerGroup(string groupName);

    IZLinkClientServerChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;

    IZLinkClientServerChannelBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkClientServerChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;

    IZLinkClientServerChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkClientServerChannelBuilder EnableSpotRouteEgress(string targetSpotNodeChannelName);
}

public interface IZLinkFanoutChannelBuilder
{
    IZLinkFanoutChannelBuilder EnablePublisher(string endpoint);

    IZLinkFanoutChannelBuilder EnableSubscriber();

    IZLinkFanoutChannelBuilder EnableSubscriber(string endpoint);

    IZLinkFanoutChannelBuilder AddHandlerGroup(string groupName);

    IZLinkFanoutChannelBuilder AddPublishHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkPublishHandler<TMessage>;

    IZLinkFanoutChannelBuilder AddPublishHandler<THandler>(string? packetName = null)
        where THandler : class;
}

public interface IZLinkDealerMeshChannelBuilder
{
    // dealer mesh 는 DEALER ↔ DEALER 대칭이라 server·client 가 같은 DEALER 소켓을 공유한다.
    // EnableServer 는 이 노드가 bind 해서 inbound 를 handler 로 받는(제공) 쪽을,
    // EnableClient 는 다른 peer 에 connect 해 outbound 호출하는(소비) 쪽을 설정한다.
    // 한 노드가 둘 다 켤 수 있다.
    IZLinkDealerMeshChannelBuilder EnableServer(string endpoint);

    IZLinkDealerMeshChannelBuilder EnableClient();

    IZLinkDealerMeshChannelBuilder EnableClient(string endpoint);

    IZLinkDealerMeshChannelBuilder AddHandlerGroup(string groupName);

    IZLinkDealerMeshChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;

    IZLinkDealerMeshChannelBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkDealerMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;

    IZLinkDealerMeshChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;
}

public interface IZLinkSpotNodeBuilder
{
    IZLinkSpotNodeBuilder EnableRouter(string endpoint);

    IZLinkSpotNodeBuilder ConnectRouter(string endpoint);

    IZLinkSpotNodeBuilder ConnectRouter(RoutingId peerRid, string endpoint);

    IZLinkSpotNodeBuilder SetRouterRoutingId(RoutingId routingId);

    IZLinkSocketConfig ConfigureRouterSocket();

    IZLinkRouteConfig ConfigureRouterRouting();

    IZLinkSpotNodeBuilder EnablePubSub(string endpoint);

    IZLinkSpotNodeBuilder ConnectPubSub(string endpoint);

    IZLinkSpotNodeBuilder SetPubSubRoutingId(RoutingId routingId);

    IZLinkSpotPublisherConfig ConfigurePubSubPublisher();

    IZLinkSpotSubscriberConfig ConfigurePubSubSubscriber();

    IZLinkSpotNodeBuilder AttachChannelClient(string channelName);

    IZLinkSpotNodeBuilder AttachChannelClient(string channelName, string endpoint);

    IZLinkSocketConfig ConfigureChannelClientSocket(string channelName);

    IZLinkOutboundRouteConfig ConfigureChannelClientRouting(string channelName);

    IZLinkSpotNodeBuilder AttachSpotPublisherClient(string channelName);

    IZLinkSpotNodeBuilder AttachSpotPublisherClient(string channelName, string endpoint);

    IZLinkSocketConfig ConfigureSpotPublisherClientSocket(string channelName);

    IZLinkSpotNodeBuilder AcceptSpotRoutesFromChannel(string channelName);

    IZLinkSpotNodeBuilder AcceptSpotRoutesFromChannel(string channelName, string endpoint);

    IZLinkEntrySpotOptions ConfigureEntrySpot();

    IZLinkSpotNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot;

    IZLinkSpotNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot;
}

public interface IZLinkSpotMeshNodeBuilder : IZLinkSpotNodeBuilder
{
}

public interface IZLinkSpotMeshBuilder : IZLinkSpotNodeBuilder
{
    IZLinkDiscoveryBuilder UseDiscovery();

    IZLinkSpotMeshNodeBuilder AddNode(string spotNodeName);
}

public interface IZLinkRegistrySpotRemoteAddressesOptions
{
    string? RouterChannelId { get; set; }
}

public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultTimeout { get; set; }

    IZLinkCodecRegistryBuilder Codecs { get; }

    IZLinkWorkerOptions Worker { get; }

    void AddHandlersFromAssemblyOf<TMarker>();

    void AddHandlersFromAssemblyOf(Type markerType);

    void AddHandlersFromAssembly(System.Reflection.Assembly assembly);

    IZLinkMetadataPolicyBuilder ConfigureMetadata();

    void AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;

    void AddSpotRemoteAddressResolver<TResolver>()
        where TResolver : class, IZLinkSpotRemoteAddressResolver;

    IZLinkRegistrySpotRemoteAddressesOptions UseRegistrySpotRemoteAddresses(
        string namespaceName);

    IZLinkClientServerChannelBuilder AddClientServerChannel(string channelName);

    IZLinkFanoutChannelBuilder AddFanoutChannel(string channelName);

    IZLinkDealerMeshChannelBuilder AddDealerMeshChannel(string channelName);

    IZLinkRouteMeshChannelBuilder AddRouteMeshChannel(string channelName);

    IZLinkDiscoveryBuilder UseDiscovery();

    void UseFilter<TFilter>()
        where TFilter : class, IZLinkHandlerFilter;

    IZLinkDispatchOptions ConfigureDispatch();

    IZLinkStreamNodeBuilder AddStreamNode(string streamNodeName);

    IZLinkSpotMeshBuilder AddSpotMesh(string channelName);
}

public interface IZLinkMetadataPolicyBuilder
{
    void AddForwardedMetadataKey(string key);
}
