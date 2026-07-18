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

    // Pins the single MeshName this STREAM node uses for session Actor dispatch
    // (spec 05-route-mesh §2, 31-session-actor-dispatch §2). The value is not
    // inferred from the endpoint, the first MeshNode, or an ActorRef. Calling it
    // twice on the same builder, or naming a MeshName with no local MeshNode, is a
    // startup configuration error. STREAM nodes that do not dispatch actors omit it.
    IZLinkStreamNodeBuilder EnableActorDispatch(string meshName);

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

// The 10.0.0 unified MeshNode registration surface (IZLinkMeshNodeBuilder,
// IZLinkMeshChannelBuilder and supporting types) lives in MeshNodeBuilders.cs.
// It replaces the pre-10.0.0 IZLinkSpotNodeBuilder/IZLinkSpotMeshBuilder cluster
// removed here per spec 05-route-mesh + gap 90 §12.33.

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
    /// Registers one physical location store instance for every store role,
    /// the way codecs register serializer instances. The instance may
    /// additionally implement the optional change stamp and watch contracts;
    /// they are picked up automatically. The official Redis store is the
    /// production default; the single-process in-memory store is test-only
    /// (registered via the internal test helper, spec 05-route-mesh §7 / gap
    /// 90 §12.33). Hosts that use auto discovery, distributed Spot/Actor
    /// addressing, or Actor transfer must register a store or host startup
    /// fails fast.
    /// </summary>
    void AddLocationStore(Locations.IZLinkLocationStore store);

    Locations.ZLinkLocationOptions ConfigureLocations();

    void UseFilter<TFilter>()
        where TFilter : class, IZLinkHandlerFilter;

    IZLinkDispatchOptions ConfigureDispatch();

    IZLinkStreamCompressionBuilder ConfigureStreamCompression();

    IZLinkStreamNodeBuilder AddStreamNode(string streamNodeName);

    // Registers one process-local MeshNode under meshName (spec 05-route-mesh §2).
    // Registering the same meshName twice fails host startup. The MeshNode owns its
    // ROUTER endpoint, logical channel memberships, RID-direct route handlers and
    // Spot/Actor registry.
    IZLinkMeshNodeBuilder AddRouteMesh(string meshName);
}

public interface IZLinkMetadataPolicyBuilder
{
    void AddForwardedMetadataKey(string key);
}
