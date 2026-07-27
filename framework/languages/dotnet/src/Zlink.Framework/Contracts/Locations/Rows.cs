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

    public string? EntrySpotId { get; init; }

    public int PlacementWeight { get; init; } = 100;

    public ZLinkPlacementCapacity Capacity { get; init; }
        = new(
            new ZLinkPopulationCapacity(0, 0, 0),
            new ZLinkPopulationCapacity(0, 0, 0),
            Array.Empty<ZLinkSpotTypeCapacity>());

    public ZLinkActivationConcurrency ActivationConcurrency { get; init; }
        = new(0, 128);
}

public readonly record struct ZLinkMeshNodeDescriptorKey(
    string MeshName,
    RoutingId Rid);

public sealed record ZLinkClientServerServerDescriptor(
    string ChannelName,
    RoutingId ServerRid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    int Weight,
    ZLinkFrameworkRuntimeState State,
    string SecurityIdentity,
    string OwnerId,
    long LeaseGeneration,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkClientServerServerDescriptorKey(
    string ChannelName,
    RoutingId ServerRid);

public sealed record ZLinkFanoutPublisherDescriptor(
    string ChannelName,
    RoutingId PublisherRid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    ZLinkFrameworkRuntimeState State,
    string SecurityIdentity,
    string OwnerId,
    long LeaseGeneration,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkFanoutPublisherDescriptorKey(
    string ChannelName,
    RoutingId PublisherRid);

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
    int Limit);

public sealed record ZLinkPopulationCapacity(
    int Active,
    int Reserved,
    int Limit);

public sealed record ZLinkSpotTypeCapacity(
    ZLinkPlacementObjectKind ObjectKind,
    string StableType,
    int Active,
    int Reserved,
    int Limit);

public sealed record ZLinkPlacementCapacity(
    ZLinkPopulationCapacity Actors,
    ZLinkPopulationCapacity Spots,
    IReadOnlyList<ZLinkSpotTypeCapacity> SpotTypes);

public sealed record ZLinkActivationConcurrency(
    int Active,
    int Limit);
