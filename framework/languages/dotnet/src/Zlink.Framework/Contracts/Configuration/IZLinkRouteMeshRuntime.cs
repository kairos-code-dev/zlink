using Systems.Zlink;

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

public sealed record ZLinkLogicalMulticastSnapshot(
    ulong Submitted,
    ulong Backpressured,
    ulong Dropped,
    ulong RemoteSnapshotCount,
    ulong RemoteAdmittedCount,
    ulong RemoteDroppedCount,
    ulong LocalSnapshotCount,
    ulong LocalAdmittedCount,
    ulong LocalDroppedCount);

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
    ZLinkLogicalMulticastSnapshot Multicast,
    ZLinkMeshClaimSnapshot Claims,
    ZLinkLocationRuntimeSnapshot Location,
    ZLinkMeshDrainSnapshot Drain);

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
    ulong? RemoteSnapshotCount,
    ulong? RemoteAdmittedCount,
    ulong? RemoteDroppedCount,
    ulong? LocalSnapshotCount,
    ulong? LocalAdmittedCount,
    ulong? LocalDroppedCount,
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
