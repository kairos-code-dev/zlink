namespace Zlink.Framework.Contracts.Configuration;

// 10.0.0 unified RouteMesh·MeshNode registration surface.
// Owned by spec server/languages/dotnet/05-route-mesh.ko.md §2·§3·§4·§5.
// AddRouteMesh(meshName) registers one process-local MeshNode; ChannelName(...)
// adds an immutable logical membership + handler namespace without a new socket.

public readonly record struct ZLinkMeshPeerConnection(
    string Endpoint,
    RoutingId? ExpectedRoutingId);

public interface IZLinkMeshPeerConnections
{
    void Connect(string endpoint);

    void Connect(RoutingId expectedRoutingId, string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<ZLinkMeshPeerConnection> ListConnections();
}

// Subset of IZLinkSocketConfig the MeshNode ROUTER exposes at build time
// (spec §5). HWM and timeout are startup-only; runtime-mutable options live on
// IZLinkRouteMeshRuntimeOptions.
public interface IZLinkMeshNodeSocketConfig
{
    long MaxMessageSize { get; set; }

    int SendHighWaterMark { get; set; }

    int ReceiveHighWaterMark { get; set; }

    ulong MailboxMessageBudget { get; set; }

    ulong MailboxByteBudget { get; set; }

    TimeSpan? ReceiveTimeout { get; set; }

    TimeSpan? SendTimeout { get; set; }
}

public interface IZLinkMeshChannelBuilder
{
    IZLinkMeshChannelBuilder SetWeight(int weight);

    IZLinkMeshChannelBuilder AddHandlerGroup(string groupName);

    IZLinkMeshChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;

    IZLinkMeshChannelBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;

    IZLinkMeshChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;
}

public interface IZLinkMeshObjectServerBuilder
{
    IZLinkMeshObjectServerBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : class, IZLinkEntrySpot;

    IZLinkMeshObjectServerBuilder AddSpotFactory<TSpot>(
        string spotType,
        ZLinkObjectPlacementOptions? placement,
        ZLinkRelocationPolicy<TSpot> relocation)
        where TSpot : class, IZLinkSpot;

    IZLinkMeshObjectServerBuilder AddInstanceSpotFactory<TSpot>(
        string instanceSpotType,
        ZLinkObjectPlacementOptions? placement,
        ZLinkRelocationPolicy<TSpot> relocation)
        where TSpot : class, IZLinkInstanceSpot;

    IZLinkMeshObjectServerBuilder AddActorFactory<TActor, TFactory>(
        string actorType,
        ZLinkObjectPlacementOptions? placement,
        ZLinkRelocationPolicy<TActor> relocation)
        where TActor : class, IZLinkActor
        where TFactory : class, IZLinkActorFactory<TActor>;
}

public interface IZLinkMeshObjectRoleBuilder
{
    IZLinkMeshObjectClientBuilder Client();

    IZLinkMeshObjectServerBuilder Server();
}

public interface IZLinkMeshObjectClientBuilder
{
}

public sealed record ZLinkObjectPlacementOptions
{
    public int? MaxActiveObjects { get; init; }

    public int? MaxPendingActivations { get; init; }
}

public interface IZLinkMeshNodeBuilder : IZLinkMeshObjectServerBuilder
{
    IZLinkMeshChannelBuilder ChannelName(string channelName);

    IZLinkMeshNodeBuilder Listen(string endpoint);

    IZLinkMeshNodeBuilder SetRoutingId(RoutingId routingId);

    IZLinkMeshNodeBuilder UseAllocatedRoutingId(int slotCount);

    IZLinkMeshNodeBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix);

    IZLinkMeshNodeBuilder SetRoutingIdAllocationGroup(string groupName);

    IZLinkMeshNodeBuilder SetPlacementWeight(int weight);

    IZLinkMeshNodeBuilder SetObjectCapacity(
        int maxActiveObjects,
        int maxPendingActivations);

    IZLinkMeshObjectRoleBuilder Objects();

    IZLinkMeshNodeSocketConfig ConfigureRouterSocket();

    IZLinkSpotPublisherConfig ConfigureSpotPublisher();

    IZLinkMeshPeerConnections PeerConnections { get; }

    IZLinkMeshNodeBuilder SetDefaultRequestTimeout(TimeSpan timeout);

    IZLinkMeshNodeBuilder AddRouteSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkRouteSendHandler<TMessage>;

    IZLinkMeshNodeBuilder AddRouteSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkMeshNodeBuilder AddRouteRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRouteRequestHandler<TRequest, TReply>;

    IZLinkMeshNodeBuilder AddRouteRequestHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkEntrySpotOptions ConfigureEntrySpot();

    IZLinkMeshNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot;

    IZLinkMeshNodeBuilder AddInstanceSpotFactory<TSpot>(
        string instanceSpotType,
        ZLinkInstanceSpotFactoryOptions? options = null)
        where TSpot : class, IZLinkInstanceSpot;

    new IZLinkMeshNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot;

    IZLinkMeshNodeBuilder AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;

    IZLinkMeshNodeBuilder AddActorTransferAdapter<TActor, TAdapter>(string actorType)
        where TActor : IZLinkActor
        where TAdapter : class, IZLinkActorTransferAdapter<TActor>;
}
