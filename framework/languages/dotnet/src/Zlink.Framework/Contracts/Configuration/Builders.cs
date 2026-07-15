namespace Zlink.Framework.Contracts.Configuration;

// Build-time and runtime configuration use the same option contracts. Runtime
// changes are accepted only for properties whose public contract allows them.
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

    IZLinkEndpointConnections ClientConnections { get; }

    IZLinkRouteMeshChannelBuilder SetRoutingId(RoutingId routingId);

    IZLinkRouteMeshChannelBuilder UseAllocatedRoutingId(int slotCount);

    IZLinkRouteMeshChannelBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix);

    IZLinkRouteMeshChannelBuilder SetRoutingIdAllocationGroup(string groupName);

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

    IZLinkEndpointConnections ClientConnections { get; }

    IZLinkClientServerChannelBuilder SetRoutingId(RoutingId routingId);

    IZLinkClientServerChannelBuilder UseAllocatedRoutingId(int slotCount);

    IZLinkClientServerChannelBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix);

    IZLinkClientServerChannelBuilder SetRoutingIdAllocationGroup(string groupName);

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

    IZLinkEndpointConnections SubscriberConnections { get; }

    IZLinkFanoutChannelBuilder SetRoutingId(RoutingId routingId);

    IZLinkFanoutChannelBuilder UseAllocatedRoutingId(int slotCount);

    IZLinkFanoutChannelBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix);

    IZLinkFanoutChannelBuilder SetRoutingIdAllocationGroup(string groupName);

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

    IZLinkSpotNodeBuilder UseAllocatedRoutingId(int slotCount);

    IZLinkSpotNodeBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix);

    IZLinkSpotNodeBuilder SetRoutingIdAllocationGroup(string groupName);

    IZLinkSocketConfig ConfigureRouterSocket();

    IZLinkRouteConfig ConfigureRouterRouting();

    IZLinkSpotNodeBuilder EnablePubSub(string endpoint);

    IZLinkSpotNodeBuilder ConnectPeerPub(string endpoint);

    IZLinkEndpointConnections RouterConnections { get; }

    IZLinkEndpointConnections PubSubConnections { get; }

    /// <summary>
    /// Role-oriented name for <see cref="RouterConnections"/>. Both
    /// properties expose the same logical connection set.
    /// </summary>
    IZLinkEndpointConnections ChannelClientConnections { get; }

    /// <summary>
    /// Role-oriented name for <see cref="PubSubConnections"/>. Both
    /// properties expose the same logical connection set.
    /// </summary>
    IZLinkEndpointConnections PublisherConnections { get; }

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

    IZLinkSpotNodeBuilder AddActorTransferAdapter<TActor, TAdapter>(string actorType)
        where TActor : IZLinkActor
        where TAdapter : class, IZLinkActorTransferAdapter<TActor>;
}

public interface IZLinkSpotMeshBuilder : IZLinkSpotNodeBuilder
{
    IZLinkSpotMeshBuilder UseDrainPolicy(ZLinkSpotDrainPolicy policy);
}

public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultRequestTimeout { get; set; }

    /// <summary>
    ///     Gets or sets how long a source node forwards packets sent through
    ///     the actor reference that was current before a remote transfer. The
    ///     default is five seconds. Zero disables forwarding after the commit;
    ///     negative values are rejected.
    /// </summary>
    TimeSpan ActorTransferForwardWindow { get; set; }

    TimeSpan? DefaultSocketSendTimeout { get; set; }

    IZLinkCodecRegistryBuilder Codecs { get; }

    IZLinkWorkerOptions Worker { get; }

    void AddHandlersFromAssemblyOf<TMarker>();

    void AddHandlersFromAssemblyOf(Type markerType);

    void AddHandlersFromAssembly(System.Reflection.Assembly assembly);

    void DisableImplicitHandlerAutoRegistration();

    IZLinkMetadataPolicyBuilder ConfigureMetadata();

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
    /// location store role, the way codecs register serializer instances. The
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
