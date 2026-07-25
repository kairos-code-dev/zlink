namespace Zlink.Framework.Contracts.Configuration;

// 10.0.0 unified RouteMesh·MeshNode registration surface.
// Owned by spec server/languages/dotnet/05-route-mesh.ko.md §2·§3·§4·§5.
// AddRouteMesh(meshName) registers one process-local MeshNode. Channel(...)
// adds an immutable logical membership without creating another socket.

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

public interface IZLinkMeshChannelRoleBuilder
{
    IZLinkMeshChannelClientBuilder Client();

    IZLinkMeshChannelServerBuilder Server();
}

public interface IZLinkMeshChannelClientBuilder
{
}

public interface IZLinkMeshChannelServerBuilder
{
    IZLinkMeshChannelServerBuilder SetWeight(int weight);

    IZLinkMeshChannelServerBuilder AddHandlerGroup(string groupName);

    IZLinkMeshChannelServerBuilder AddSendHandler<THandler, TMessage>(string? packetName = null)
        where THandler : class, IZLinkSendHandler<TMessage>;

    IZLinkMeshChannelServerBuilder AddSendHandler<THandler>(string? packetName = null)
        where THandler : class;

    IZLinkMeshChannelServerBuilder AddRequestHandler<THandler, TRequest, TReply>(
        string? packetName = null)
        where THandler : class, IZLinkRequestHandler<TRequest, TReply>;

    IZLinkMeshChannelServerBuilder AddRequestHandler<THandler>(string? packetName = null)
        where THandler : class;
}

public interface IZLinkMeshObjectServerBuilder
{
    IZLinkMeshObjectServerBuilder AddEntrySpot<TEntrySpot>()
        where TEntrySpot : class, IZLinkEntrySpot;

    IZLinkMeshObjectServerBuilder AddSpotFactory<TSpot>(
        string spotType,
        ZLinkUserSpotFactoryOptions? options,
        ZLinkRelocationPolicy<TSpot> relocation)
        where TSpot : class, IZLinkSpot;

    IZLinkMeshObjectServerBuilder AddInstanceSpotFactory<TSpot>(
        string instanceSpotType,
        ZLinkInstanceSpotFactoryOptions? options,
        ZLinkRelocationPolicy<TSpot> relocation)
        where TSpot : class, IZLinkInstanceSpot;

    IZLinkMeshObjectServerBuilder AddActorFactory<TActor, TFactory>(
        string actorType,
        ZLinkActorFactoryOptions? options,
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

public sealed record ZLinkActorFactoryOptions
{
}

public enum ZLinkUserSpotExecutionMode
{
    SpotWide = 0,
    PerActor = 1
}

public sealed record ZLinkUserSpotFactoryOptions
{
    public int StableTypeLimit { get; init; }

    public ZLinkUserSpotExecutionMode ExecutionMode { get; init; }
        = ZLinkUserSpotExecutionMode.SpotWide;
}

public interface IZLinkMeshNodeBuilder
{
    IZLinkMeshChannelRoleBuilder Channel(string channelName);

    IZLinkMeshNodeBuilder Listen(string endpoint);

    IZLinkMeshNodeBuilder Listen(int port = 0);

    IZLinkMeshNodeBuilder SetBindHost(string bindHost);

    IZLinkMeshNodeBuilder SetAdvertiseHost(string advertiseHost);

    IZLinkMeshNodeBuilder SetRoutingId(RoutingId routingId);

    IZLinkMeshNodeBuilder SetRoutingIdPrefix(string prefix);

    IZLinkMeshNodeBuilder SetPlacementWeight(int weight);

    IZLinkMeshNodeBuilder SetActorLimit(int limit);

    IZLinkMeshNodeBuilder SetSpotLimit(int limit);

    IZLinkMeshNodeBuilder SetActivationConcurrency(int limit);

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

}
