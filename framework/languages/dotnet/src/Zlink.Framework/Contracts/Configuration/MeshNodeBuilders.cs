namespace Zlink.Framework.Contracts.Configuration;

// 10.0.0 unified RouteMesh·MeshNode registration surface.
// Owned by spec server/languages/dotnet/05-route-mesh.ko.md §2·§3·§4·§5.
// AddRouteMesh(meshName) registers one process-local MeshNode; ChannelName(...)
// adds an immutable logical membership + handler namespace without a new socket.

public enum ZLinkMeshNodeDrainPolicy
{
    DrainNatural = 0,
    ReleaseAndRecreate = 1
}

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

    TimeSpan? ReceiveTimeout { get; set; }

    TimeSpan? SendTimeout { get; set; }
}

public interface IZLinkMeshChannelBuilder
{
    IZLinkMeshChannelBuilder SetWeight(int weight);

    IZLinkMeshChannelBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;

    IZLinkMeshChannelBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkMeshChannelBuilder AddRequestHandler<THandler, TRequest, TReply>(string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;

    IZLinkMeshChannelBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;
}

public interface IZLinkMeshNodeBuilder
{
    IZLinkMeshChannelBuilder ChannelName(string channelName);

    IZLinkMeshNodeBuilder Listen(string endpoint);

    IZLinkMeshNodeBuilder SetRoutingId(RoutingId routingId);

    IZLinkMeshNodeBuilder UseAllocatedRoutingId(int slotCount);

    IZLinkMeshNodeBuilder UseAllocatedRoutingId(int slotCount, string routingIdPrefix);

    IZLinkMeshNodeBuilder SetRoutingIdAllocationGroup(string groupName);

    IZLinkMeshNodeSocketConfig ConfigureRouterSocket();

    IZLinkSpotPublisherConfig ConfigureSpotPublisher();

    IZLinkMeshNodeBuilder UseDrainPolicy(ZLinkMeshNodeDrainPolicy policy);

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

    IZLinkMeshNodeBuilder SetEntrySpotRoutingId(RoutingId routingId);

    IZLinkMeshNodeBuilder AddSpotFactory<TSpot>()
        where TSpot : IZLinkSpot;

    IZLinkMeshNodeBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : IZLinkEntrySpot;

    IZLinkMeshNodeBuilder AddActorFactory<TFactory>(string actorType)
        where TFactory : class, IZLinkActorFactory;

    IZLinkMeshNodeBuilder AddActorTransferAdapter<TActor, TAdapter>(string actorType)
        where TActor : IZLinkActor
        where TAdapter : class, IZLinkActorTransferAdapter<TActor>;
}
