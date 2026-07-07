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

public interface IZLinkStreamCompressionBuilder
{
    IZLinkStreamCompressionBuilder UseDefault();

    IZLinkStreamCompressionBuilder UseLz4();

    IZLinkStreamCompressionBuilder Use(IZlinkStreamCompressionCodec codec);

    IZLinkStreamCompressionBuilder Disable();
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
}

public interface IZLinkStreamNodeBuilder
{
    IZLinkStreamNodeBuilder Bind(string endpoint);

    IZLinkStreamNodeBuilder SetTlsServer(string certPath, string keyPath, bool requireClientCert = false);

    IZLinkStreamNodeBuilder RegisterSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkClientServerChannelBuilder : IZLinkClientServerChannelOptions
{
    // ConfigureServerSocket/ClientSocket/ServerRouting/ClientRouting 은 IZLinkClientServerChannelOptions 상속.
    IZLinkClientServerChannelBuilder EnableServer(string endpoint);

    IZLinkClientServerChannelBuilder EnableClient();

    IZLinkClientServerChannelBuilder EnableClient(string endpoint);

    IZLinkClientServerChannelBuilder SetRoutingId(RoutingId routingId);

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
}

public interface IZLinkFanoutChannelBuilder
{
    IZLinkFanoutChannelBuilder EnablePublisher(string endpoint);

    IZLinkFanoutChannelBuilder EnableSubscriber();

    IZLinkFanoutChannelBuilder EnableSubscriber(string endpoint);

    IZLinkFanoutChannelBuilder SetRoutingId(RoutingId routingId);

    IZLinkFanoutChannelBuilder AddHandlerGroup(string groupName);

    IZLinkFanoutChannelBuilder AddPublishHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkPublishHandler<TMessage>;

    IZLinkFanoutChannelBuilder AddPublishHandler<THandler>(string? packetName = null)
        where THandler : class;
}

public interface IZLinkSpotNodeBuilder
{
    IZLinkSpotNodeBuilder EnableRouter(string endpoint);

    IZLinkSpotNodeBuilder ConnectRouter(string endpoint);

    IZLinkSpotNodeBuilder ConnectRouter(RoutingId peerRid, string endpoint);

    IZLinkSpotNodeBuilder SetRoutingId(RoutingId routingId);

    IZLinkSocketConfig ConfigureRouterSocket();

    IZLinkRouteConfig ConfigureRouterRouting();

    IZLinkSpotNodeBuilder EnablePubSub(string endpoint);

    IZLinkSpotNodeBuilder ConnectPeerPub(string endpoint);

    IZLinkSpotPublisherConfig ConfigurePubSubPublisher();

    IZLinkSpotSubscriberConfig ConfigurePubSubSubscriber();

    IZLinkEntrySpotOptions ConfigureEntrySpot();

    IZLinkSpotNodeBuilder SetEntrySpotRoutingId(RoutingId routingId);

    IZLinkSpotNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot;

    IZLinkSpotNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot;

    IZLinkSpotNodeBuilder AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;
}

public interface IZLinkSpotMeshBuilder : IZLinkSpotNodeBuilder;

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

    void AddSpotRouteRefResolver<TResolver>()
        where TResolver : class, IZLinkSpotRouteRefResolver;

    IZLinkClientServerChannelBuilder AddClientServerChannel(string channelName);

    IZLinkFanoutChannelBuilder AddFanoutChannel(string channelName);

    IZLinkRouteMeshChannelBuilder AddRouteMeshChannel(string channelName);

    /// <summary>
    /// Registers the framework's single-process in-memory store for every
    /// location store role. For local development, unit tests, and sample
    /// smoke tests only — never for multi-process production topologies.
    /// </summary>
    void UseInMemoryLocationStores();

    /// <summary>
    /// Registers one physical location store instance for every store role
    /// (draft 20.2), the way codecs register serializer instances. The
    /// instance may additionally implement the optional change stamp and
    /// watch contracts; they are picked up automatically. Mutually
    /// exclusive with UseInMemoryLocationStores.
    /// </summary>
    void AddLocationStore(Locations.IZLinkLocationStore store);

    Locations.ZLinkLocationOptions ConfigureLocations();

    void UseFilter<TFilter>()
        where TFilter : class, IZLinkHandlerFilter;

    IZLinkDispatchOptions ConfigureDispatch();

    IZLinkStreamCompressionBuilder ConfigureStreamCompression();

    IZLinkStreamNodeBuilder AddStreamNode(string streamNodeName);

    IZLinkSpotMeshBuilder AddSpotMesh(string channelName);
}

public interface IZLinkMetadataPolicyBuilder
{
    void AddForwardedMetadataKey(string key);
}
