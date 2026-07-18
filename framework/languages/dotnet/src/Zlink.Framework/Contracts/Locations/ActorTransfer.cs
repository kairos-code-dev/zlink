namespace Zlink.Framework.Contracts.Locations;

// Durable Actor transfer authority (spec server/41-location-store-redis §3.1,
// server/languages/dotnet/06-location-store §6, gap 90 §12.29). One physical
// store arbitrates which node owns an actor across the prepared → commit →
// abort state machine, admits exactly one active transfer per actor, and makes
// a crashed coordinator's prepared transfer recoverable through recovery-lease
// expiry. The interface never takes a Core-issued sealed token: the framework
// transfer coordinator pairs this distributed authority with the same-process
// Core prepare/commit primitives (Runtime/Backend/Contracts/IZLinkBackendActorTransfer).

/// <summary>
/// Lifecycle state of one Actor transfer record. Prepared and Committed are the
/// live states; Activated and Aborted are terminal and preserved for recovery
/// auditing after the active index is cleared.
/// </summary>
public enum ZLinkActorTransferState
{
    Prepared = 1,
    Committed = 2,
    Activated = 3,
    Aborted = 4
}

/// <summary>
/// A durable Actor transfer record. Generations and the membership epoch are
/// unsigned to match the Core fence values and the Redis 64-bit encoding.
/// <see cref="Participants"/> is the CAS participant set the coordinator fenced
/// at prepare time.
/// </summary>
public sealed record ZLinkActorTransferRecord(
    string MeshName,
    string ActorId,
    Guid TransferId,
    ActorRef Source,
    ActorRef Target,
    ulong ExpectedActorGeneration,
    ulong ExpectedMembershipEpoch,
    IReadOnlySet<RoutingId> Participants,
    ZLinkActorTransferState State,
    string RecoveryOwnerId,
    DateTimeOffset RecoveryLeaseExpiresAt,
    DateTimeOffset UpdatedAt);

/// <summary>
/// Prepare arguments. <see cref="RecoveryLeaseTtl"/> is a relative duration; the
/// store computes the absolute <see cref="ZLinkActorTransferRecord.RecoveryLeaseExpiresAt"/>
/// from its own clock, so callers never produce an absolute expiry time.
/// </summary>
public sealed record ZLinkActorTransferPrepareRequest(
    string MeshName,
    string ActorId,
    Guid TransferId,
    ActorRef Source,
    ActorRef Target,
    ulong ExpectedActorGeneration,
    ulong ExpectedMembershipEpoch,
    IReadOnlySet<RoutingId> Participants,
    string RecoveryOwnerId,
    TimeSpan RecoveryLeaseTtl);

/// <summary>
/// Result status of a transfer state transition. Expected coordination races
/// are returned here; store connection/command failures are surfaced as
/// exceptions the same as location reads and writes.
/// </summary>
public enum ZLinkActorTransferWriteStatus
{
    Stored = 1,
    NotFound = 2,
    IgnoredStale = 3,
    RejectedConflict = 4,
    InvalidState = 5
}

public sealed record ZLinkActorTransferWriteResult(
    ZLinkActorTransferWriteStatus Status,
    ZLinkActorTransferRecord? Record)
{
    public static ZLinkActorTransferWriteResult NotFound { get; } =
        new(ZLinkActorTransferWriteStatus.NotFound, null);

    public static ZLinkActorTransferWriteResult IgnoredStale { get; } =
        new(ZLinkActorTransferWriteStatus.IgnoredStale, null);

    public static ZLinkActorTransferWriteResult RejectedConflict { get; } =
        new(ZLinkActorTransferWriteStatus.RejectedConflict, null);

    public static ZLinkActorTransferWriteResult InvalidState { get; } =
        new(ZLinkActorTransferWriteStatus.InvalidState, null);

    public static ZLinkActorTransferWriteResult Stored(ZLinkActorTransferRecord record) =>
        new(ZLinkActorTransferWriteStatus.Stored, record);
}

/// <summary>
/// Distributed Actor transfer authority. Every transition is one atomic store
/// operation. Exactly one active transfer is allowed per actor; terminal
/// records are preserved but drop out of the active index.
/// </summary>
public interface IZLinkActorTransferStore
{
    /// <summary>
    /// Fences a new transfer: succeeds only when the actor has no active
    /// transfer. Records the expected actor generation, membership epoch and
    /// participant set that later transitions validate against.
    /// </summary>
    ValueTask<ZLinkActorTransferWriteResult> PrepareActorTransferAsync(
        ZLinkActorTransferPrepareRequest request,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Commits a Prepared transfer when the recovery-lease owner matches,
    /// advancing the membership epoch by one and moving the record to
    /// Committed.
    /// </summary>
    ValueTask<ZLinkActorTransferWriteResult> CommitActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Activates a Committed transfer, clearing the active index so a later
    /// transfer of the same actor can be prepared.
    /// </summary>
    ValueTask<ZLinkActorTransferWriteResult> ActivateActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Aborts a Prepared transfer, preserving the source owner and membership
    /// epoch, and clears the active index.
    /// </summary>
    ValueTask<ZLinkActorTransferWriteResult> AbortActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Takes over a transfer whose recovery lease has expired, moving the
    /// recovery lease to the successor owner in one atomic operation. The
    /// successor re-runs Core prepare on its own runtime to mint a fresh sealed
    /// token; this store never carries a Core token across processes.
    /// </summary>
    ValueTask<ZLinkActorTransferWriteResult> TakeOverActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string successorOwnerId,
        TimeSpan recoveryLeaseTtl,
        CancellationToken cancellationToken = default);

    /// <summary>
    /// Returns the current active transfer record for an actor, or null when no
    /// active transfer exists.
    /// </summary>
    ValueTask<ZLinkActorTransferRecord?> ResolveActorTransferAsync(
        string meshName,
        string actorId,
        CancellationToken cancellationToken = default);
}
