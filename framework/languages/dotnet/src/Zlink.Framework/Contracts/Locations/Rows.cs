namespace Zlink.Framework.Contracts.Locations;

/// <summary>
///     One MeshNode's published physical identity: endpoint plus the whole
///     immutable ChannelName membership with its mutable weights. One
///     descriptor per (MeshName, Rid); never one row per channel
///     (06-location-store §4).
/// </summary>
public sealed record ZLinkMeshNodeDescriptor(
    string MeshName,
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    IReadOnlyDictionary<string, int> ChannelWeights,
    bool Draining,
    string SecurityIdentity,
    string OwnerId,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkMeshNodeDescriptorKey(
    string MeshName,
    RoutingId Rid);

/// <summary>
///     Current location of one logical Spot. The owner MeshNode's RID and
///     lifecycle generation ride along so resolvers only trust the row while
///     the same-generation descriptor and owner lease are both live.
/// </summary>
public sealed record ZLinkSpotLocation(
    string MeshName,
    RoutingId SpotRid,
    ulong SpotGeneration,
    RoutingId OwnerNodeRid,
    ulong OwnerNodeGeneration,
    ZLinkSpotKind SpotKind,
    string SpotType,
    string OwnerId,
    DateTimeOffset UpdatedAt);

/// <summary>
///     Current location of one actor: the distributed owner and membership
///     epoch authority (server/23-spot-actor §1).
/// </summary>
public sealed record ZLinkActorLocation(
    string MeshName,
    string ActorId,
    string ActorType,
    ActorRef ActorRef,
    RoutingId OwnerNodeRid,
    ulong OwnerNodeGeneration,
    RoutingId SpotRid,
    ulong SpotGeneration,
    ZLinkSpotKind SpotKind,
    ulong MembershipEpoch,
    string OwnerId,
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
