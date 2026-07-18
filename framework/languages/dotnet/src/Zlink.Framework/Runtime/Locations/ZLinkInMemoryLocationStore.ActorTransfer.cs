namespace Zlink.Framework.Runtime.Locations;

// In-memory Actor transfer authority (spec server/41-location-store-redis §3.1,
// gap 90 §12.29). Single-process only: it exercises the prepare → commit →
// activate/abort state machine, single-active-transfer admission, and recovery
// lease takeover for contract tests. Distributed fencing and crash recovery
// across processes are only meaningful on the Redis store; this implementation
// gives the same visible transitions on one clock so the state machine can be
// verified without a live Redis.
internal sealed partial class ZLinkInMemoryLocationStore
{
    private readonly Dictionary<string, ActorTransferSlot> _actorTransfers =
        new(StringComparer.Ordinal);

    public ValueTask<ZLinkActorTransferWriteResult> PrepareActorTransferAsync(
        ZLinkActorTransferPrepareRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            var key = TransferKey(request.MeshName, request.ActorId);
            if (!_actorTransfers.TryGetValue(key, out var slot))
            {
                slot = new ActorTransferSlot();
                _actorTransfers.Add(key, slot);
            }

            if (slot.ActiveTransferId is { } active)
            {
                // Exactly one active transfer per actor. A repeat of the same
                // prepare is idempotent; a different transfer id is a race.
                if (active == request.TransferId
                    && slot.Records.TryGetValue(active, out var existing)
                    && existing.State == ZLinkActorTransferState.Prepared)
                {
                    return ValueTask.FromResult(ZLinkActorTransferWriteResult.Stored(existing));
                }

                return ValueTask.FromResult(ZLinkActorTransferWriteResult.RejectedConflict);
            }

            var record = new ZLinkActorTransferRecord(
                request.MeshName,
                request.ActorId,
                request.TransferId,
                request.Source,
                request.Target,
                request.ExpectedActorGeneration,
                request.ExpectedMembershipEpoch,
                request.Participants.ToHashSet(),
                ZLinkActorTransferState.Prepared,
                request.RecoveryOwnerId,
                now + request.RecoveryLeaseTtl,
                now);
            slot.Records[request.TransferId] = record;
            slot.ActiveTransferId = request.TransferId;
            return ValueTask.FromResult(ZLinkActorTransferWriteResult.Stored(record));
        }
    }

    public ValueTask<ZLinkActorTransferWriteResult> CommitActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Transition(
            meshName,
            actorId,
            transferId,
            recoveryOwnerId,
            requiredState: ZLinkActorTransferState.Prepared,
            clearsActiveIndex: false,
            // Commit advances the membership epoch by one on the actor row; the
            // record keeps the fence prepare validated and moves to Committed.
            // NOTE: the actor location row rewrite (target owner + epoch+1) is
            // coupled to the actor-row shape gap (90 §12.27 — ZLinkActorLocation
            // has no OwnerNodeGeneration/MembershipEpoch/SpotGeneration yet), so
            // only the transfer authority state is advanced here.
            static (record, now) => record with
            {
                State = ZLinkActorTransferState.Committed,
                UpdatedAt = now
            }));

    public ValueTask<ZLinkActorTransferWriteResult> ActivateActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Transition(
            meshName,
            actorId,
            transferId,
            recoveryOwnerId,
            requiredState: ZLinkActorTransferState.Committed,
            clearsActiveIndex: true,
            static (record, now) => record with
            {
                State = ZLinkActorTransferState.Activated,
                UpdatedAt = now
            }));

    public ValueTask<ZLinkActorTransferWriteResult> AbortActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(Transition(
            meshName,
            actorId,
            transferId,
            recoveryOwnerId,
            requiredState: ZLinkActorTransferState.Prepared,
            clearsActiveIndex: true,
            static (record, now) => record with
            {
                State = ZLinkActorTransferState.Aborted,
                UpdatedAt = now
            }));

    public ValueTask<ZLinkActorTransferWriteResult> TakeOverActorTransferAsync(
        string meshName,
        string actorId,
        Guid transferId,
        string successorOwnerId,
        TimeSpan recoveryLeaseTtl,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            if (!TryGetRecord(meshName, actorId, transferId, out var slot, out var record))
                return ValueTask.FromResult(ZLinkActorTransferWriteResult.NotFound);

            // Only a still-live transfer can be taken over, and only once its
            // recovery lease has expired on the store clock.
            if (record.State is not (ZLinkActorTransferState.Prepared or ZLinkActorTransferState.Committed))
                return ValueTask.FromResult(ZLinkActorTransferWriteResult.InvalidState);
            if (record.RecoveryLeaseExpiresAt > now)
                return ValueTask.FromResult(ZLinkActorTransferWriteResult.RejectedConflict);

            var updated = record with
            {
                RecoveryOwnerId = successorOwnerId,
                RecoveryLeaseExpiresAt = now + recoveryLeaseTtl,
                UpdatedAt = now
            };
            slot.Records[transferId] = updated;
            return ValueTask.FromResult(ZLinkActorTransferWriteResult.Stored(updated));
        }
    }

    public ValueTask<ZLinkActorTransferRecord?> ResolveActorTransferAsync(
        string meshName,
        string actorId,
        CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            var key = TransferKey(meshName, actorId);
            if (_actorTransfers.TryGetValue(key, out var slot)
                && slot.ActiveTransferId is { } active
                && slot.Records.TryGetValue(active, out var record))
            {
                return ValueTask.FromResult<ZLinkActorTransferRecord?>(record);
            }

            return ValueTask.FromResult<ZLinkActorTransferRecord?>(null);
        }
    }

    private ZLinkActorTransferWriteResult Transition(
        string meshName,
        string actorId,
        Guid transferId,
        string recoveryOwnerId,
        ZLinkActorTransferState requiredState,
        bool clearsActiveIndex,
        Func<ZLinkActorTransferRecord, DateTimeOffset, ZLinkActorTransferRecord> advance)
    {
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            if (!TryGetRecord(meshName, actorId, transferId, out var slot, out var record))
                return ZLinkActorTransferWriteResult.NotFound;
            if (record.State != requiredState)
                return ZLinkActorTransferWriteResult.InvalidState;
            if (!string.Equals(record.RecoveryOwnerId, recoveryOwnerId, StringComparison.Ordinal))
                return ZLinkActorTransferWriteResult.RejectedConflict;

            var updated = advance(record, now);
            slot.Records[transferId] = updated;
            if (clearsActiveIndex && slot.ActiveTransferId == transferId)
                slot.ActiveTransferId = null;
            return ZLinkActorTransferWriteResult.Stored(updated);
        }
    }

    private bool TryGetRecord(
        string meshName,
        string actorId,
        Guid transferId,
        out ActorTransferSlot slot,
        out ZLinkActorTransferRecord record)
    {
        if (_actorTransfers.TryGetValue(TransferKey(meshName, actorId), out slot!)
            && slot.Records.TryGetValue(transferId, out record!))
        {
            return true;
        }

        slot = null!;
        record = null!;
        return false;
    }

    private static string TransferKey(string meshName, string actorId) =>
        $"{meshName.Length}:{meshName}:{actorId}";

    private sealed class ActorTransferSlot
    {
        public Dictionary<Guid, ZLinkActorTransferRecord> Records { get; } = [];

        public Guid? ActiveTransferId { get; set; }
    }
}
