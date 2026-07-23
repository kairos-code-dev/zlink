namespace Zlink.Framework.Contracts.Configuration;

// Build-time and runtime configuration use the same option contracts. Runtime
// changes are accepted only for properties whose public contract allows them.
public interface IZLinkStreamCompressionBuilder
{
    IZLinkStreamCompressionBuilder UseDefault();

    IZLinkStreamCompressionBuilder UseLz4();

    IZLinkStreamCompressionBuilder Use(IZlinkStreamCompressionCodec codec);

    IZLinkStreamCompressionBuilder Disable();
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

    IZLinkStreamNodeBuilder SetTlsServer(
        string certificatePath,
        string keyPath,
        bool requireClientCertificate = false);

    IZLinkStreamNodeBuilder AddSession<TSession>()
        where TSession : class, IZLinkSession;
}

public interface IZLinkFanoutChannelBuilder
{
    IZLinkFanoutChannelBuilder EnablePublisher(string endpoint);

    IZLinkFanoutChannelBuilder ConnectSubscriber(string endpoint);

    IZLinkFanoutChannelBuilder AddHandler<THandler, TEvent>(string? packetName = null)
        where THandler : class, IZLinkFanoutHandler<TEvent>;
}

public interface IZLinkClientServerChannelRoleBuilder
{
    IZLinkClientServerChannelClientBuilder Client();

    IZLinkClientServerChannelServerBuilder Server();
}

public interface IZLinkClientServerChannelClientBuilder
{
    IZLinkClientServerChannelClientBuilder Connect(string endpoint);
}

public interface IZLinkClientServerChannelServerBuilder
{
    IZLinkClientServerChannelServerBuilder Listen(int port = 0);

    IZLinkClientServerChannelServerBuilder SetBindHost(string bindHost);

    IZLinkClientServerChannelServerBuilder SetAdvertiseHost(string advertiseHost);

    IZLinkClientServerChannelServerBuilder SetWeight(int weight);

    IZLinkClientServerChannelServerBuilder AddHandlerGroup(string groupName);

    IZLinkClientServerChannelServerBuilder AddSendHandler<THandler, TMessage>(
        string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;

    IZLinkClientServerChannelServerBuilder AddRequestHandler<THandler, TRequest, TReply>(
        string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;
}

// The unified MeshNode registration surface and supporting types live in
// MeshNodeBuilders.cs.

public interface IZLinkFrameworkOptions
{
    TimeSpan DefaultRequestTimeout { get; set; }

    /// <summary>
    ///     Gets or sets how long a source node forwards packets sent through
    ///     the actor reference that was current before a remote transfer. The
    ///     default is five seconds. Zero disables forwarding after the commit;
    ///     negative values are rejected.
    /// </summary>
    TimeSpan? ActorTransferTimeout { get; set; }

    TimeSpan? ActorTransferForwardWindow { get; set; }

    TimeSpan DefaultSocketSendTimeout { get; set; }

    long ApplicationVersion { get; set; }

    string? MaintenanceWave { get; set; }

    IZLinkCodecRegistryBuilder Codecs { get; }

    IZLinkWorkerOptions Worker { get; }

    void AddHandlersFromAssemblyOf<TMarker>();

    void AddHandlersFromAssemblyOf(Type markerType);

    void AddHandlersFromAssembly(System.Reflection.Assembly assembly);

    void DisableImplicitHandlerAutoRegistration();

    IZLinkMetadataPolicyBuilder ConfigureMetadata();

    IZLinkFanoutChannelBuilder AddFanoutChannel(string channelName);

    IZLinkClientServerChannelRoleBuilder AddClientServerChannel(string channelName);

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

    void AddRelocationStore(Locations.IZLinkRelocationStore store);

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
    IZLinkMetadataPolicyBuilder AllowSessionToActor(string key);

    IZLinkMetadataPolicyBuilder AllowActorToSession(string key);
}
