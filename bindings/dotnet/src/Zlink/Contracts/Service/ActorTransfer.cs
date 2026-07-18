// SPDX-License-Identifier: MPL-2.0

using Systems.Zlink.Runtime.Native;

namespace Systems.Zlink;

/// <summary>
///     The side of an actor transfer fence. Maps to
///     <c>zlink_actor_transfer_role_t</c>.
/// </summary>
public enum ActorTransferRole
{
    /// <summary>The node currently hosting the actor.</summary>
    Source = 1,

    /// <summary>The node the actor is transferring to.</summary>
    Target = 2
}

/// <summary>
///     A phase in the actor transfer fence protocol. Maps to
///     <c>zlink_actor_transfer_phase_t</c>.
/// </summary>
public enum ActorTransferPhase
{
    /// <summary>Prepare submitted; reservations forming.</summary>
    Preparing = 1,

    /// <summary>Fenced: no new admission, sequence frozen.</summary>
    Fenced = 2,

    /// <summary>Committed to the new membership epoch.</summary>
    Committed = 3,

    /// <summary>Activated on the target.</summary>
    Activated = 4,

    /// <summary>Aborted; the fence is released.</summary>
    Aborted = 5
}

/// <summary>
///     Identifies an actor transfer. Maps to
///     <c>zlink_actor_transfer_id_t</c>.
/// </summary>
/// <param name="High">The high 64 bits.</param>
/// <param name="Low">The low 64 bits.</param>
public readonly record struct ActorTransferId(ulong High, ulong Low);

/// <summary>
///     An opaque, framework-owned actor transfer fence token returned by
///     <see cref="IMeshNode.PrepareActorTransfer" /> and passed to commit,
///     activate, or abort. Maps to <c>zlink_actor_transfer_token_t</c>.
/// </summary>
public readonly struct ActorTransferToken
{
    internal ActorTransferToken(ZlinkActorTransferToken native)
    {
        Native = native;
    }

    internal ZlinkActorTransferToken Native { get; }
}

/// <summary>
///     Inputs to <see cref="IMeshNode.PrepareActorTransfer" />. Maps to
///     <c>zlink_actor_transfer_prepare_t</c>.
/// </summary>
/// <param name="Role">Whether this node is the transfer source or target.</param>
/// <param name="TransferId">The transfer identity shared by both sides.</param>
/// <param name="Actor">The actor being transferred.</param>
/// <param name="ExpectedMembershipEpoch">The membership epoch the caller expects.</param>
/// <param name="PeerNodeRid">The other node's routing id.</param>
/// <param name="FinalSequence">The final application sequence to fence at.</param>
/// <param name="ReserveMessageCount">Messages to reserve for hand-off.</param>
/// <param name="ReserveByteCount">Bytes to reserve for hand-off.</param>
public readonly record struct ActorTransferPrepare(
    ActorTransferRole Role,
    ActorTransferId TransferId,
    ActorRef Actor,
    ulong ExpectedMembershipEpoch,
    RoutingId PeerNodeRid,
    ulong FinalSequence,
    ulong ReserveMessageCount,
    ulong ReserveByteCount);

/// <summary>
///     The result of <see cref="IMeshNode.PrepareActorTransfer" />. Maps to
///     <c>zlink_actor_transfer_prepare_result_t</c>.
/// </summary>
/// <param name="Role">The fenced role.</param>
/// <param name="TransferId">The transfer identity.</param>
/// <param name="Actor">The actor being transferred.</param>
/// <param name="FinalSequence">The fenced final application sequence.</param>
/// <param name="ReserveMessageCount">The reserved message count.</param>
/// <param name="ReserveByteCount">The reserved byte count.</param>
public readonly record struct ActorTransferPrepareResult(
    ActorTransferRole Role,
    ActorTransferId TransferId,
    ActorRef Actor,
    ulong FinalSequence,
    ulong ReserveMessageCount,
    ulong ReserveByteCount);

/// <summary>
///     A transfer-control record surfaced as the typed payload of a
///     <see cref="MeshRecordKind.TransferControl" /> receive record. Maps to
///     <c>zlink_actor_transfer_control_t</c>.
/// </summary>
/// <param name="Phase">The current transfer phase.</param>
/// <param name="Role">The role this record targets.</param>
/// <param name="TransferId">The transfer identity.</param>
/// <param name="Actor">The actor being transferred.</param>
/// <param name="MembershipEpoch">The membership epoch at this phase.</param>
/// <param name="FinalSequence">The fenced final application sequence.</param>
/// <param name="ResultCode">The terminal result, or 0.</param>
/// <param name="FailureErrno">The failure errno, or 0.</param>
public readonly record struct ActorTransferControl(
    ActorTransferPhase Phase,
    ActorTransferRole Role,
    ActorTransferId TransferId,
    ActorRef Actor,
    ulong MembershipEpoch,
    ulong FinalSequence,
    int ResultCode,
    int FailureErrno);
