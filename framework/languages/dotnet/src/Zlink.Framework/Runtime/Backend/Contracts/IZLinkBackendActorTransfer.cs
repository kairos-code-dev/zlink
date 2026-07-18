namespace Zlink.Framework.Runtime.Backend.Contracts;

// Framework-owned mirror of the bindings actor-transfer fence surface
// (Systems.Zlink ActorTransfer*), mapped in ZLinkBackendSpotNodeWrapper. These
// are the native/local primitives of the RouteMesh 10.0.0 transfer protocol
// (prepare fence, commit epoch, activate, abort). The distributed authority that
// orchestrates them across nodes (participant-set CAS, transfer token lease,
// prepared/commit/abort crash recovery) is S8-04A (Redis) and is not implemented
// here; the existing join-based handoff behavior is preserved unchanged.

internal enum ZLinkBackendActorTransferRole
{
    Source = 1,
    Target = 2
}

internal enum ZLinkBackendActorTransferPhase
{
    Preparing = 1,
    Fenced = 2,
    Committed = 3,
    Activated = 4,
    Aborted = 5
}

internal readonly record struct ZLinkBackendActorTransferId(ulong High, ulong Low);

// Opaque, framework-owned fence token round-tripped through commit/activate/abort.
internal readonly struct ZLinkBackendActorTransferToken
{
    internal ZLinkBackendActorTransferToken(ActorTransferToken native)
    {
        Native = native;
    }

    internal ActorTransferToken Native { get; }
}

internal readonly record struct ZLinkBackendActorTransferPrepare(
    ZLinkBackendActorTransferRole Role,
    ZLinkBackendActorTransferId TransferId,
    ZLinkBackendActorRef Actor,
    ulong ExpectedMembershipEpoch,
    RoutingId PeerNodeRid,
    ulong FinalSequence,
    ulong ReserveMessageCount,
    ulong ReserveByteCount);

internal readonly record struct ZLinkBackendActorTransferPrepareResult(
    ZLinkBackendActorTransferRole Role,
    ZLinkBackendActorTransferId TransferId,
    ZLinkBackendActorRef Actor,
    ulong FinalSequence,
    ulong ReserveMessageCount,
    ulong ReserveByteCount);

// The typed payload of a TransferControl receive record, delivered by the node
// dispatch pump to the registered transfer-control handler.
internal readonly record struct ZLinkBackendActorTransferControl(
    ZLinkBackendActorTransferPhase Phase,
    ZLinkBackendActorTransferRole Role,
    ZLinkBackendActorTransferId TransferId,
    ZLinkBackendActorRef Actor,
    ulong MembershipEpoch,
    ulong FinalSequence,
    int ResultCode,
    int FailureErrno);
