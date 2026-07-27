namespace SpotActorTransfer.Shared;

public static class SpotActorTransferNames
{
    public const string Mesh = "spot-actor-transfer";
    public const string ActorTypeStateful = "transfer-stateful";
    public const string ActorTypeEmptyState = "transfer-empty-state";
    public const string ActorTypeNoAdapter = "transfer-no-adapter";
    public const string ActorTypeFailTransferOut = "transfer-fail-out";
    public const string ActorTypeFailLeave = "transfer-fail-leave";
    public const string ActorTypeFailTransferIn = "transfer-fail-in";
    public const string UserSpotType = "transfer-user-spot";
    public const string RelocationPayloadUserSpotType =
        "relocation-payload-user-spot";
    public const string RelocationPayloadInstanceSpotType =
        "relocation-payload-instance-spot";

}

public sealed record ActorCreateReq(
    string ActorId,
    string ActorType,
    int StateVersion,
    int ApplicationStateBytes = 0);

public sealed record ActorCreateRes(
    string ActorId,
    string ActorType,
    string NodeRid,
    long Generation);

public sealed record CreateSpotReq(
    string SpotId,
    string Mode = "accept");

public sealed record CreateSpotRes(
    string SpotId,
    string NodeRid,
    string State);

public sealed record MeshReadyRes(
    string NodeRid,
    string[] ReadyPeerRids,
    string[] ReadySpotTypes);

public sealed record PlacementWeightReq(int Weight);

public sealed record PlacementWeightRes(int Weight);

public sealed record GateReleaseRes(
    string SpotId,
    bool Released);

public sealed record CleanupGateArmReq(
    string Scenario);

public sealed record CleanupGateRes(
    string ActorId,
    bool Changed);

public sealed record JoinTargetReq(
    string Scenario,
    string TargetSpotId,
    string ExpectedMode = "accept");

public sealed record JoinTargetRes(
    string Scenario,
    string ActorId,
    bool Accepted,
    string SourceNodeRid,
    string TargetSpotId,
    int StateVersion);

public sealed record ProbeReq(
    string Scenario,
    string Marker);

public sealed record HandoffPacket(
    string Scenario,
    string Marker);

public sealed record ProbeRes(
    string Scenario,
    string ActorId,
    string SpotId,
    string NodeRid,
    int StateVersion,
    string Marker);

public sealed record NodeActorCallReq(
    string Scenario,
    string Marker,
    int TimeoutMs = 5000,
    string? TransportOperationId = null);

public sealed record NodeActorProbeRes(
    bool Succeeded,
    ProbeRes? Reply,
    string? ErrorKind);

public sealed record BindActorSessionReq(
    string Scenario,
    string ActorId,
    string? NodeRid = null,
    long? Generation = null);

public sealed record BindActorSessionRes(
    string Scenario,
    string ActorId,
    string NodeRid,
    long Generation);

public sealed record SessionBindingsReq(string Scenario);

public sealed record SessionBindingSnapshot(
    string ActorId,
    string NodeRid,
    long Generation);

public sealed record SessionBindingsRes(
    string Scenario,
    SessionBindingSnapshot[] Bindings);

public sealed record BoundPushReq(
    string Scenario,
    string Marker,
    string? ActorId = null);

public sealed record BoundPushRes(
    string Scenario,
    string ActorId,
    string SpotId,
    string NodeRid,
    string Marker,
    int StateVersion);

public sealed record BoundPushNotify(
    string Scenario,
    string ActorId,
    string SpotId,
    string NodeRid,
    string Marker,
    int StateVersion);

public sealed record EvidenceWaitReq(
    string[] ContainsAll,
    int TimeoutMilliseconds = 10000);

public sealed record ActorRefSnapshotRes(
    string ActorId,
    string NodeRid,
    long Generation);

public sealed record ActorDestroyRes(
    string ActorId,
    long Generation,
    bool Destroyed);

public sealed record ActorEvidence(
    string Scenario,
    string ActorId,
    string Kind,
    string Value,
    string NodeRid,
    long ObservedUnixTimeMilliseconds);

public sealed record RelocationBlobMeasurement(
    string Operation,
    int EncodedBytes,
    string PayloadSha256,
    string OpaqueReferenceSha256);

public sealed record RelocationPayloadSpotReq(
    string Scenario,
    int ApplicationStateBytes);

public sealed record RelocationPayloadSpotRes(
    string SpotId,
    string NodeRid,
    int ApplicationStateBytes,
    string ApplicationStateSha256);

public sealed record RelocateHostReq(
    long? TargetApplicationVersion = null,
    int DeadlineMilliseconds = 120000);

public sealed record RelocateHostRes(
    string Outcome,
    string Reason,
    string State);

public sealed record ProcessMemoryRes(
    long WorkingSetBytes,
    long PeakWorkingSetBytes);

public sealed record RelocationWorkloadRequest(
    string Scenario,
    long Sequence,
    string OperationId,
    long SentUnixTimeMilliseconds,
    long AbsoluteDeadlineUnixTimeMilliseconds);

public sealed record RelocationWorkloadReply(
    string Scenario,
    long Sequence,
    string OperationId,
    string? CorrelationId,
    string TargetId,
    string NodeRid,
    long ObjectGeneration,
    bool WithinDeadline,
    long HandledUnixTimeMilliseconds);

public sealed record RelocationWorkloadPacket(
    string Scenario,
    long Sequence,
    string OperationId,
    long SentUnixTimeMilliseconds,
    long AbsoluteDeadlineUnixTimeMilliseconds);

public sealed record RelocationWorkloadCallReq(
    string TargetId,
    string Scenario,
    long Sequence,
    string OperationId,
    long SentUnixTimeMilliseconds,
    long AbsoluteDeadlineUnixTimeMilliseconds,
    int TimeoutMilliseconds = 5000);

public sealed record RelocationLocationQueryReq(
    string[] ActorIds,
    string[] SpotIds);

public sealed record RelocationLocationSnapshot(
    string ObjectKind,
    string ObjectId,
    long ObjectGeneration,
    string NodeRid);

public sealed record RelocationUnitTerminalEvidence(
    string ObjectKind,
    string ObjectId,
    long ObjectGeneration,
    string NodeRid,
    long ObservedUnixTimeMilliseconds);

public static class RelocationWorkloadMetadata
{
    public const string OperationId =
        "zlink-e2e-relocation-operation-id";
}

public sealed record RelocationBulkActorCreateReq(
    string Scenario,
    string ActorIdPrefix,
    string ActorType,
    int Count,
    int ApplicationStateBytes,
    int MaxConcurrency = 64);

public sealed record RelocationBulkActorCreateRes(
    string[] ActorIds,
    string[] NodeRids);

public sealed record RelocationBulkSpotCreateReq(
    string Scenario,
    string SpotIdPrefix,
    int Count,
    int ApplicationStateBytes,
    bool InstanceSpot,
    int MaxConcurrency = 64,
    int ActorsPerSpot = 0);

public sealed record RelocationBulkSpotCreateRes(
    string[] SpotIds,
    string[] NodeRids,
    string[] ActorIds);

public sealed record TransportDeliveryArmReq(
    string ActorId,
    string Kind);

public sealed record TransportDeliveryGateRes(
    string OperationId,
    string ActorId,
    string Kind,
    int CapturedCount,
    int ReleasedCount,
    bool Armed,
    bool Released);
