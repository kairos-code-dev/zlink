// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>RouteMesh events selectable when a MeshNode monitor is opened.</summary>
public enum MeshMonitorEventKind
{
    StateChanged = 1,
    PeerConnecting = 2,
    PeerAdmitted = 3,
    PeerDraining = 4,
    PeerClosed = 5,
    PeerRejected = 6,
    ChannelChanged = 7,
    MessageSubmitted = 8,
    MulticastCommitted = 9,
    MulticastDropped = 10,
    Backpressured = 11,
    OperationCompleted = 12,
    ProtocolError = 13,
    ClaimRevoked = 14
}

/// <summary>Bit mask used to select MeshNode monitor events.</summary>
[Flags]
public enum MeshMonitorEventMask : ulong
{
    None = 0,
    StateChanged = 1UL << 0,
    PeerConnecting = 1UL << 1,
    PeerAdmitted = 1UL << 2,
    PeerDraining = 1UL << 3,
    PeerClosed = 1UL << 4,
    PeerRejected = 1UL << 5,
    ChannelChanged = 1UL << 6,
    MessageSubmitted = 1UL << 7,
    MulticastCommitted = 1UL << 8,
    MulticastDropped = 1UL << 9,
    Backpressured = 1UL << 10,
    OperationCompleted = 1UL << 11,
    ProtocolError = 1UL << 12,
    ClaimRevoked = 1UL << 13,
    All = (1UL << 14) - 1
}

/// <summary>One event received from a MeshNode monitor.</summary>
public sealed record MeshMonitorEvent(
    MeshMonitorEventKind Kind,
    ulong TimestampMs,
    ulong MeshLifecycleGeneration,
    ulong MeshDescriptorRevision,
    MeshNodeState MeshState,
    RoutingId PeerRid,
    ulong PeerLifecycleGeneration,
    ulong PeerDescriptorRevision,
    MeshOwnerKind OwnerKind,
    RoutingId SpotRid,
    ActorRef Actor,
    string ChannelName,
    MeshOperationId OperationId,
    uint SnapshotRemoteTargetCount,
    uint AdmittedRemoteTargetCount,
    uint DroppedRemoteTargetCount,
    uint UnreachableRemoteTargetCount,
    uint SnapshotLocalSpotCount,
    uint AdmittedLocalSpotCount,
    uint DroppedLocalSpotCount,
    int ResultCode,
    int FailureErrno);

/// <summary>A snapshot of MeshNode monitor counters.</summary>
public sealed record MeshMonitorStatus(
    MeshNodeState State,
    ulong PeerAdmitted,
    ulong PeerRejected,
    ulong SubmittedMessages,
    ulong CompletedOperations,
    ulong BackpressuredSubmits,
    ulong MulticastMessages,
    ulong MulticastDroppedTargets,
    ulong ActiveClaims,
    ulong PendingApplicationMessages,
    ulong PendingInfrastructureMessages,
    ulong PendingBytes);

/// <summary>Receives push events from one MeshNode.</summary>
public interface IMeshNodeMonitor : IDisposable, IAsyncDisposable
{
    /// <summary>
    ///     Receives one event. A nonblocking receive returns null when no event
    ///     is ready.
    /// </summary>
    MeshMonitorEvent? Recv(RecvFlags flags = RecvFlags.None);

    /// <summary>Reads the current monitor counters.</summary>
    MeshMonitorStatus Status();

    /// <summary>Closes the monitor.</summary>
    void Close();
}
