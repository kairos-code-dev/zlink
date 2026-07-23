using Zlink.Framework.Contracts.Configuration;

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
    string SecurityIdentity,
    string OwnerId,
    long LeaseGeneration,
    DateTimeOffset UpdatedAt)
{
    public long ApplicationVersion { get; init; }

    public IReadOnlyList<ZLinkObjectCapability> ObjectCapabilities { get; init; }
        = Array.Empty<ZLinkObjectCapability>();

    public string? MaintenanceWave { get; init; }

    public ZLinkFrameworkRuntimeState State { get; init; }

    public ZLinkMeshNodeObjectRole ObjectRole { get; init; }

    public int PlacementWeight { get; init; } = 100;

    public ZLinkPlacementCapacity Capacity { get; init; }
        = new(0, 0, 10_000, 128);
}

public readonly record struct ZLinkMeshNodeDescriptorKey(
    string MeshName,
    RoutingId Rid);

public enum ZLinkMeshNodeObjectRole
{
    None = 0,
    Client = 1,
    Server = 2
}

public enum ZLinkObjectMaintenancePolicyKind
{
    Disabled = 1,
    Recreate = 2,
    Snapshot = 3
}

public sealed record ZLinkObjectCapability(
    ZLinkPlacementObjectKind ObjectKind,
    string StableType,
    ZLinkObjectMaintenancePolicyKind Policy,
    bool HasSnapshotAdapter,
    IReadOnlySet<string> PlacementProfiles,
    int? ActiveLimit,
    int? PendingLimit);

public sealed record ZLinkPlacementCapacity(
    int Active,
    int Pending,
    int ActiveLimit,
    int PendingLimit);

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
    DateTimeOffset UpdatedAt)
{
    public ulong AuthorityOwnerGeneration { get; init; }
}

public sealed record InstanceSpotLocation(
    string MeshName,
    RoutingId SpotRid,
    ulong SpotGeneration,
    RoutingId OwnerNodeRid,
    ulong OwnerNodeGeneration,
    string InstanceSpotType,
    ZLinkSpotActivationState ActivationState,
    ulong ActivationEpoch,
    string OwnerId,
    ulong LocationGeneration,
    DateTimeOffset UpdatedAt);

public sealed record InstanceSpotClaimRequest(
    string MeshName,
    RoutingId SpotRid,
    string InstanceSpotType,
    RoutingId TargetNodeRid,
    ulong TargetNodeGeneration,
    string OwnerId);

public sealed record InstanceSpotLeaseSnapshot(
    DateTimeOffset LeaseExpiresAt,
    DateTimeOffset StoreNow);

public sealed record InstanceSpotSnapshot(
    InstanceSpotLocation Location,
    InstanceSpotLeaseSnapshot Lease);

public abstract record InstanceSpotClaimResult
{
    private InstanceSpotClaimResult()
    {
    }

    public sealed record Claimed(InstanceSpotSnapshot Snapshot)
        : InstanceSpotClaimResult;
    public sealed record Existing(InstanceSpotSnapshot Snapshot)
        : InstanceSpotClaimResult;
    public sealed record Conflict : InstanceSpotClaimResult;
}

public abstract record InstanceSpotWriteResult
{
    private InstanceSpotWriteResult()
    {
    }

    public sealed record Stored(InstanceSpotSnapshot Snapshot)
        : InstanceSpotWriteResult;
    public sealed record Stale : InstanceSpotWriteResult;
    public sealed record Conflict : InstanceSpotWriteResult;
}

public abstract record InstanceSpotResolveResult
{
    private InstanceSpotResolveResult()
    {
    }

    public sealed record Found(InstanceSpotSnapshot Snapshot)
        : InstanceSpotResolveResult;
    public sealed record Missing : InstanceSpotResolveResult;
}

public sealed record InstanceSpotFence(
    string MeshName,
    RoutingId SpotRid,
    string OwnerId,
    ulong OwnerNodeGeneration,
    ulong LocationGeneration,
    ulong ActivationEpoch);

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
    DateTimeOffset UpdatedAt)
{
    public ulong AuthorityOwnerGeneration { get; init; }
}

/// <summary>
/// One lease per framework runtime instance. Location rows are live only
/// while their owner's lease is live.
/// </summary>
public sealed record ZLinkOwnerLease(
    string OwnerId,
    RoutingId NodeRid,
    DateTimeOffset LeaseExpiresAt,
    DateTimeOffset UpdatedAt)
{
    public long LeaseGeneration { get; init; }
}

/// <summary>
/// Owner lease list plus the store's current time. Expiry is judged from
/// <see cref="StoreNow"/> and locally measured monotonic elapsed time.
/// </summary>
public sealed record ZLinkOwnerLeaseSnapshot(
    IReadOnlyList<ZLinkOwnerLease> Leases,
    DateTimeOffset StoreNow);
