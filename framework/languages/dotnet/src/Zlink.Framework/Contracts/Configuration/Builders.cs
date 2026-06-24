namespace Zlink.Framework.Contracts.Configuration;

// Per-kind 옵션 인터페이스: 빌더의 ConfigureXxx() 표면을 추출한 것. build-time(빌더가 상속)과
// runtime(IZLinkChannelRuntimeOptions 가 반환) 이 같은 인터페이스를 공유한다. 같은 인터페이스라도
// runtime impl 은 live socket backing 이라 startup 전용 속성 set 은 오류로 거부한다.
public interface IZLinkClientServerChannelOptions
{
    IZLinkSocketConfig ConfigureServerSocket();

    IZLinkRouteConfig ConfigureServerRouting();

    IZLinkSocketConfig ConfigureClientSocket();

    IZLinkOutboundRouteConfig ConfigureClientRouting();
}

public interface IZLinkRouteMeshChannelOptions
{
    IZLinkSocketConfig ConfigureSocket();
}

public interface IZLinkDealerMeshChannelOptions
{
    // dealer mesh 는 server·client 가 같은 DEALER 소켓을 공유하므로 단일 ConfigureSocket 만 가진다.
    IZLinkSocketConfig ConfigureSocket();
}

public interface IZLinkDiscoveryBuilder
{
    IZLinkDiscoveryBuilder AddRegistryEndpoint(string endpoint);
}

public interface IZLinkRouteMeshChannelBuilder : IZLinkRouteMeshChannelOptions
{
    // route mesh 는 ROUTER ↔ ROUTER 대칭이라 server·client 가 같은 ROUTER 소켓을 공유한다.
    // EnableServer 는 이 노드가 bind 해서 route ingress 를 받는(제공) 쪽을,
    // EnableClient 는 다른 ROUTER 에 connect 해 outbound route 를 보내는(소비) 쪽을 설정한다.
    // 한 노드가 둘 다 켤 수 있다. ConfigureSocket() 은 IZLinkRouteMeshChannelOptions 상속.
    IZLinkRouteMeshChannelBuilder EnableServer(string endpoint);

    IZLinkRouteMeshChannelBuilder EnableClient();

    IZLinkRouteMeshChannelBuilder EnableClient(string endpoint);

    IZLinkRouteMeshChannelBuilder SetRoutingId(RoutingId routingId);

    IZLinkRouteMeshChannelBuilder SetDefaultRequestTimeout(TimeSpan timeout);

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

    IZLinkStreamNodeBuilder SetTlsServer(string certPath, string keyPath, bool requireClientCert = false);

    IZLinkStreamNodeBuilder AttachActorGateway(string spotNodeName);

    IZLinkStreamNodeBuilder RegisterSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkClientServerChannelBuilder : IZLinkClientServerChannelOptions
{
    // ConfigureServerSocket/ClientSocket/ServerRouting/ClientRouting 은 IZLinkClientServerChannelOptions 상속.
    IZLinkClientServerChannelBuilder EnableServer(string endpoint);

    IZLinkClientServerChannelBuilder EnableClient();

    IZLinkClientServerChannelBuilder EnableClient(string endpoint);

    IZLinkClientServerChannelBuilder SetDefaultRequestTimeout(TimeSpan timeout);

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

public interface IZLinkDealerMeshChannelBuilder : IZLinkDealerMeshChannelOptions
{
    // dealer mesh 는 DEALER ↔ DEALER 대칭이라 server·client 가 같은 DEALER 소켓을 공유한다.
    // EnableServer 는 이 노드가 bind 해서 inbound 를 handler 로 받는(제공) 쪽을,
    // EnableClient 는 다른 peer 에 connect 해 outbound 호출하는(소비) 쪽을 설정한다.
    // 한 노드가 둘 다 켤 수 있다. ConfigureSocket() 은 IZLinkDealerMeshChannelOptions 상속.
    IZLinkDealerMeshChannelBuilder EnableServer(string endpoint);

    IZLinkDealerMeshChannelBuilder EnableClient();

    IZLinkDealerMeshChannelBuilder EnableClient(string endpoint);

    IZLinkDealerMeshChannelBuilder SetDefaultRequestTimeout(TimeSpan timeout);

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

    IZLinkSpotNodeBuilder ConnectPeerPub(string endpoint);

    IZLinkSpotNodeBuilder ConnectPubSub(string endpoint);

    IZLinkSpotNodeBuilder SetPubSubRoutingId(RoutingId routingId);

    IZLinkSpotPublisherConfig ConfigurePubSubPublisher();

    IZLinkSpotSubscriberConfig ConfigurePubSubSubscriber();

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

    IZLinkSpotMeshBuilder UseRegistrySpotResolver();

    IZLinkSpotMeshNodeBuilder AddNode(string spotNodeName);
}

public interface IZLinkRegistrySpotRemoteAddressesOptions
{
    string? RouterChannelId { get; set; }
}

public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultRequestTimeout { get; set; }

    TimeSpan? DefaultSocketSendTimeout { get; set; }

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
