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
    public const string UserSpotTypePrefix = "transfer-user-spot";

    public static string UserSpotType(string nodeRid) =>
        $"{UserSpotTypePrefix}:{nodeRid}";
}

public sealed record ActorCreateReq(
    string ActorId,
    string ActorType,
    int StateVersion);

public sealed record ActorCreateRes(
    string ActorId,
    string ActorType,
    string NodeRid,
    long Generation);

public sealed record CreateSpotReq(
    string SpotId,
    string Mode = "accept",
    string? TargetNodeRid = null);

public sealed record CreateSpotRes(
    string SpotId,
    string NodeRid,
    string State);

public sealed record MeshReadyRes(
    string NodeRid,
    string[] ReadyPeerRids,
    string[] ReadySpotTypes);

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

public sealed record ActorRefProbeReq(
    string Scenario,
    string Marker,
    string NodeRid,
    long Generation,
    int TimeoutMs = 5000);

public sealed record ActorRefProbeRes(
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

public sealed record TransferStateDto(
    string ActorId,
    int StateVersion);

public sealed record ActorEvidence(
    string Scenario,
    string ActorId,
    string Kind,
    string Value,
    string NodeRid);
