// SPDX-License-Identifier: MPL-2.0

namespace Systems.Zlink;

/// <summary>
///     The kind of an actor lifecycle transition. Maps to
///     <c>zlink_actor_lifecycle_kind_t</c>.
/// </summary>
public enum ActorLifecycleKind
{
    /// <summary>The actor was created.</summary>
    Created = 1,

    /// <summary>The actor joined a spot.</summary>
    Joined = 2,

    /// <summary>The actor left a spot.</summary>
    Left = 3,

    /// <summary>The actor's bound session disconnected.</summary>
    Disconnected = 4,

    /// <summary>The actor was destroyed.</summary>
    Destroyed = 5
}

/// <summary>
///     Where a send-ready record says traffic may now be pushed. Maps to
///     <c>zlink_mesh_destination_kind_t</c>.
/// </summary>
public enum MeshDestinationKind
{
    /// <summary>A node.</summary>
    Node = 1,

    /// <summary>A channel.</summary>
    Channel = 2,

    /// <summary>A spot.</summary>
    Spot = 3,

    /// <summary>An actor.</summary>
    Actor = 4,

    /// <summary>A bound STREAM session.</summary>
    BoundSession = 5
}

/// <summary>
///     Marker for the typed payload carried in a receive record's
///     <c>kind_data</c>. Read it from <see cref="MeshReceiveRecord.KindData" />
///     or the record's typed accessors. The concrete type depends on the
///     record kind.
/// </summary>
public abstract record MeshRecordPayload;

/// <summary>
///     The typed payload of a <see cref="MeshRecordKind.SpotControl" /> actor
///     lifecycle record. Maps to <c>zlink_actor_control_record_t</c>.
/// </summary>
public sealed record ActorControlRecord(
    ActorLifecycleKind Kind,
    ActorRef PreviousActor,
    ActorRef CurrentActor,
    RoutingId PreviousSpotRid,
    RoutingId CurrentSpotRid,
    ulong PreviousSpotGeneration,
    ulong CurrentSpotGeneration,
    ulong PreviousMembershipEpoch,
    ulong CurrentMembershipEpoch,
    int ResultCode) : MeshRecordPayload;

/// <summary>
///     The typed payload of an actor-join <see cref="MeshRecordKind.Completion" />
///     record. Maps to <c>zlink_actor_join_completion_t</c>.
/// </summary>
public sealed record ActorJoinCompletion(
    ActorJoinResult JoinResult,
    ActorRef Actor,
    ActorLocation Location) : MeshRecordPayload;

/// <summary>
///     The typed payload of a <see cref="MeshRecordKind.SendReady" /> record.
///     Maps to <c>zlink_mesh_send_ready_data_t</c>.
/// </summary>
public sealed record MeshSendReadyData(
    MeshDestinationKind DestinationKind,
    RoutingId TargetNodeRid,
    RoutingId TargetSpotRid,
    ActorRef TargetActor,
    string? ChannelName) : MeshRecordPayload;

/// <summary>
///     The typed payload of a <see cref="MeshRecordKind.TransferControl" />
///     record. Maps to <c>zlink_actor_transfer_control_t</c>.
/// </summary>
public sealed record ActorTransferControlRecord(
    ActorTransferControl Control) : MeshRecordPayload;
