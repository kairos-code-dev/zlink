using Systems.Zlink;
using Zlink.Framework.Contracts.Locations;

namespace Zlink.Framework.Contracts.Configuration;

public enum ZLinkMeshNodeState
{
    Starting = 0,
    Serving = 1,
    Draining = 2,
    Drained = 3,
    ForceStopping = 4,
    Stopped = 5,
    Faulted = 6
}

public sealed record ZLinkMeshPeerSnapshot(
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    string AdmissionState,
    bool Ready,
    string DrainState,
    IReadOnlyList<string> ChannelNames,
    string? LastFailure);

public sealed record ZLinkMeshChannelSnapshot(
    string ChannelName,
    int LocalWeight,
    int ReadyMemberCount,
    bool Selectable);

public sealed record ZLinkMeshClaimSnapshot(
    bool ApplicationActive,
    ulong PendingApplicationWork,
    bool InfrastructureActive,
    ulong PendingInfrastructureWork);

public sealed record ZLinkMeshDrainSnapshot(
    ZLinkMeshNodeState State,
    DateTimeOffset? Deadline,
    bool WorkSealed,
    ulong PendingRequestCount,
    ulong PendingTransferCount,
    ulong PendingStreamBarrierCount);

public sealed record ZLinkLocationRuntimeSnapshot(
    string State,
    DateTimeOffset? LastSuccessAt,
    DateTimeOffset? LastFailureAt);

public sealed record ZLinkMeshNodeSnapshot(
    string MeshName,
    RoutingId Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    ZLinkMeshNodeState State,
    ulong Sequence,
    DateTimeOffset ObservedAt,
    IReadOnlyList<string> DescriptorSources,
    IReadOnlyList<ZLinkMeshPeerSnapshot> Peers,
    IReadOnlyList<ZLinkMeshChannelSnapshot> Channels,
    ZLinkMeshClaimSnapshot Claims,
    ZLinkLocationRuntimeSnapshot Location,
    ZLinkMeshDrainSnapshot Drain)
{
    public long ApplicationVersion { get; init; }
    public ZLinkMeshNodeObjectRole ObjectRole { get; init; }
    public int PlacementWeight { get; init; } = 100;
    public ZLinkPlacementCapacity PopulationCapacity { get; init; }
        = new(
            new ZLinkPopulationCapacity(0, 0, 0),
            new ZLinkPopulationCapacity(0, 0, 0),
            Array.Empty<ZLinkSpotTypeCapacity>());
    public ZLinkActivationConcurrency ActivationConcurrency { get; init; }
        = new(0, 128);
    public ulong PlacementReservationFailureCount { get; init; }
    public string? LastPlacementReservationFailure { get; init; }
    public IReadOnlyList<ZLinkObjectCapability> ObjectCapabilities { get; init; }
        = Array.Empty<ZLinkObjectCapability>();
}

public sealed record ZLinkMeshRuntimeEvent(
    string Identifier,
    ulong Sequence,
    DateTimeOffset Timestamp,
    string MeshName,
    RoutingId SourceRid,
    RoutingId? PeerRid,
    ulong? LifecycleGeneration,
    ulong? DescriptorRevision,
    string? ChannelName,
    string? ClaimDomain,
    string? MessageKind,
    string? PlacementOutcome,
    ZLinkCapacityVector? Capacity,
    ZLinkPlacementCapacity? PopulationCapacity,
    ZLinkActivationConcurrency? ActivationConcurrency,
    string? Reason,
    ZLinkMeshNodeState? State) : Zlink.Framework.Contracts.Eventing.IZLinkRuntimeEvent
{
    /// <summary>The runtime event source is the observed mesh.</summary>
    public string SourceName => MeshName;
}

public abstract record ZLinkMeshDrainResult
{
    private protected ZLinkMeshDrainResult() { }

    public sealed record Drained : ZLinkMeshDrainResult;

    public sealed record ForceStopped(string Reason) : ZLinkMeshDrainResult;
}

/// <summary>
/// Runtime monitoring service for registered RouteMesh nodes (spec 50):
/// one consistent MeshNode snapshot per MeshName, an ordered event stream,
/// and the shared graceful-drain entry point. Event identifiers and closed
/// state values are owned by the runtime-monitoring spec; drain terminal
/// reasons by the graceful-drain spec.
/// </summary>
public interface IZLinkRouteMeshRuntime
{
    ZLinkMeshNodeSnapshot Snapshot(string meshName);

    IAsyncEnumerable<ZLinkMeshRuntimeEvent> ObserveAsync(
        string meshName,
        int capacity = 1024,
        CancellationToken cancellationToken = default);

    bool IsReady(string meshName);

    /// <summary>`deadline == null` is 30 seconds. The first call fixes the
    /// shared drain deadline; later calls and <see cref="AwaitDrainedAsync"/>
    /// await the same terminal result. Cancellation ends only that waiter.</summary>
    ValueTask<ZLinkMeshDrainResult> DrainAsync(
        string meshName,
        TimeSpan? deadline = null,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkMeshDrainResult> AwaitDrainedAsync(
        string meshName,
        CancellationToken cancellationToken = default);
}
