namespace Zlink.Framework.Contracts.Locations;

public sealed record ZLinkPeerLocation(
    ZLinkLocationAutoConnectType AutoConnectType,
    string MeshName,
    RoutingId? NodeRid,
    ZLinkLocationRole Role,
    string Endpoint,
    uint Weight,
    bool Draining,
    long Value,
    IReadOnlyDictionary<string, string>? Metadata,
    IReadOnlyList<string>? Capabilities,
    string OwnerId,
    long Generation,
    DateTimeOffset UpdatedAt);

public sealed record ZLinkSpotLocation(
    string MeshName,
    RoutingId SpotRid,
    string? SpotType,
    RoutingId NodeRid,
    ZLinkSpotKind SpotKind,
    string? RouteEndpoint,
    string OwnerId,
    long Generation,
    DateTimeOffset UpdatedAt);

/// <summary>
/// Current location of one actor. <see cref="LocationKind"/> identifies the
/// kind of Spot containing the actor. A null <see cref="ActorRef"/> means
/// that no routable actor reference is available.
/// </summary>
public sealed record ZLinkActorLocation(
    string ActorId,
    string? ActorType,
    ActorRef? ActorRef,
    RoutingId NodeRid,
    ZLinkSpotKind LocationKind,
    string SpotMeshName,
    RoutingId? SpotRid,
    string OwnerId,
    long Generation,
    DateTimeOffset UpdatedAt);

/// <summary>
/// Framework internal route item. <see cref="Value"/> is a framework route
/// payload and is not an application key-value storage surface.
/// </summary>
public sealed record ZLinkRouteLocation(
    ZLinkRouteKind RouteKind,
    string RouteKey,
    RoutingId OwnerNodeRid,
    string OwnerId,
    long Generation,
    ReadOnlyMemory<byte> Value,
    DateTimeOffset UpdatedAt);

/// <summary>
/// One lease per framework runtime instance. Location rows are live only
/// while their owner's lease is live.
/// </summary>
public sealed record ZLinkOwnerLease(
    string OwnerId,
    RoutingId NodeRid,
    DateTimeOffset LeaseExpiresAt,
    DateTimeOffset UpdatedAt);

/// <summary>
/// Owner lease list plus the store's current time. Expiry is judged from
/// <see cref="StoreNow"/> and locally measured monotonic elapsed time.
/// </summary>
public sealed record ZLinkOwnerLeaseSnapshot(
    IReadOnlyList<ZLinkOwnerLease> Leases,
    DateTimeOffset StoreNow);
