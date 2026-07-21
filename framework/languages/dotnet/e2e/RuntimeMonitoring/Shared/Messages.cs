namespace RuntimeMonitoring.Shared;

public static class RuntimeMonitoringNames
{
    public const string Channel = "monitor.profile";
    public const string LocationRuntimeSource = "location-runtime";
    public const string ChannelServerSource = "monitor.profile.server";
    public const string ChannelClientSource = "monitor.profile.client";
    public const string SpotChannel = "monitor.spot";
    public const string SpotNode = SpotChannel;
}

public sealed record ProfileReq(string Value, string Marker);

public sealed record ProfileRes(string Value, string ProviderRid, string Marker);

public sealed record DrainResultRes(string Result, string? Reason = null);

public sealed record MulticastProbe(string Marker, int Sequence, string Payload);

public sealed record MulticastPublishReq(
    string Marker,
    int PayloadBytes = 1024 * 1024,
    int MaxAttempts = 20000,
    bool Blocking = false,
    ulong? ExpectedRemoteDropped = null,
    ulong? ExpectedLocalDropped = null);

public sealed record MulticastPublishRes(
    string Status,
    int Sequence,
    ulong SnapshotRemote,
    ulong AdmittedRemote,
    ulong DroppedRemote,
    ulong SnapshotLocal,
    ulong AdmittedLocal,
    ulong DroppedLocal,
    ulong SubmittedTotal,
    ulong BackpressuredTotal,
    ulong DroppedTotal);

public sealed record MeshRuntimeSnapshotRes(
    string MeshName,
    string Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    string State,
    ulong Sequence,
    DateTimeOffset ObservedAt,
    string[] DescriptorSources,
    MeshRuntimePeerRes[] Peers,
    MeshRuntimeChannelRes[] Channels,
    MeshRuntimeMulticastRes Multicast,
    MeshRuntimeClaimsRes Claims,
    MeshRuntimeLocationRes Location,
    MeshRuntimeDrainRes Drain);

public sealed record MeshRuntimePeerRes(
    string Rid,
    ulong LifecycleGeneration,
    ulong DescriptorRevision,
    string Endpoint,
    string AdmissionState,
    bool Ready,
    string DrainState,
    string[] ChannelNames,
    string? LastFailure);

public sealed record MeshRuntimeChannelRes(
    string ChannelName,
    int LocalWeight,
    int ReadyMemberCount,
    bool Selectable);

public sealed record MeshRuntimeMulticastRes(
    ulong Submitted,
    ulong Backpressured,
    ulong Dropped,
    ulong RemoteSnapshotCount,
    ulong RemoteAdmittedCount,
    ulong RemoteDroppedCount,
    ulong LocalSnapshotCount,
    ulong LocalAdmittedCount,
    ulong LocalDroppedCount);

public sealed record MeshRuntimeClaimsRes(
    bool ApplicationActive,
    ulong PendingApplicationWork,
    bool InfrastructureActive,
    ulong PendingInfrastructureWork);

public sealed record MeshRuntimeLocationRes(
    string State,
    DateTimeOffset? LastSuccessAt,
    DateTimeOffset? LastFailureAt);

public sealed record MeshRuntimeDrainRes(
    string State,
    DateTimeOffset? Deadline,
    bool WorkSealed,
    ulong PendingRequestCount,
    ulong PendingTransferCount,
    ulong PendingStreamBarrierCount);

public sealed record EvidenceWaitReq(
    string[] ContainsAll,
    string[][] ContainsAnyGroups,
    int TimeoutMilliseconds = 10000,
    int AfterIndex = 0);

public sealed record ObserverIsolationStatusRes(
    bool Running,
    bool SlowConsumerReleased,
    bool SlowConsumerFailed,
    int NormalEventCount,
    ulong NormalLatestSequence,
    ulong SlowLatestSequence,
    bool NormalSequenceGapObserved);

public sealed record RuntimeValidationRes(
    bool MissingSnapshotRejected,
    bool MissingObserverRejected,
    bool ZeroCapacityRejected);
