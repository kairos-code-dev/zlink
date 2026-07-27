using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace Zlink.Framework.Runtime.Locations;

internal sealed partial class ZLinkProviderLocationRepository
{
    private const string AuthorityPrefix = Prefix + "authority:";
    private const string ReservationPrefix = Prefix + "creation-reservation:";
    private const string TerminalPrefix = Prefix + "creation-terminal:";
    private const string RelocationCapacityPrefix = Prefix + "relocation-capacity:";
    private const string AggregatePrefix = Prefix + "aggregate:";
    private const string AggregateLockPrefix = Prefix + "aggregate-lock:";

    public async ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken = default)
    {
        ValidateAuthorityKey(key);
        var stored = await ReadAuthorityRecordAsync(key, cancellationToken)
            .ConfigureAwait(false);
        return stored is null
            ? new ZLinkAuthorityReadResult.Missing(
                await ReadStoreNowAsync(cancellationToken).ConfigureAwait(false))
            : new ZLinkAuthorityReadResult.Found(stored.Snapshot);
    }

    public async ValueTask<ZLinkAuthorityCompareExchangeResult>
        CompareExchangeAuthorityAsync(
            ZLinkAuthorityKey key,
            string expectedStoreVersion,
            ZLinkAuthorityMutation mutation,
            CancellationToken cancellationToken = default)
    {
        ValidateAuthorityKey(key);
        ArgumentException.ThrowIfNullOrWhiteSpace(expectedStoreVersion);
        ArgumentNullException.ThrowIfNull(mutation);
        ValidateAuthorityMutation(mutation);

        var current = await ReadAuthorityRecordAsync(key, cancellationToken)
            .ConfigureAwait(false);
        if (current is null
            || current.Snapshot.Allocation.State
            != ZLinkPlacementAllocationState.Active
            || current.Version.Value != expectedStoreVersion
            || await IsAggregateLockedAsync(key, cancellationToken)
                .ConfigureAwait(false))
            return Conflict(current);

        var metaKey = AuthorityMetaKey(key);
        var payloadKey = AuthorityPayloadKey(key);
        if (mutation is ZLinkAuthorityMutation.Restore restore)
        {
            ValidateAuthorityPayload(restore.Payload);
            if (current.Snapshot.OwnerId != restore.ExpectedOwner.OwnerId
                || current.Snapshot.OwnerLeaseGeneration
                != restore.ExpectedOwner.LeaseGeneration)
                return Conflict(current);
            return await StoreAuthorityAsync(
                    current,
                    current.Meta with { PayloadSha256 = Sha256(restore.Payload) },
                    restore.Payload,
                    [new ZLinkStoreCondition.Version(metaKey, current.Version)],
                    [],
                    cancellationToken)
                .ConfigureAwait(false);
        }

        if (mutation is ZLinkAuthorityMutation.Delete)
        {
            var owner = await ReadLiveOwnerAsync(
                    new ZLinkLocationOwnerToken(
                        current.Snapshot.OwnerId,
                        current.Snapshot.OwnerLeaseGeneration),
                    cancellationToken)
                .ConfigureAwait(false);
            if (owner is null) return Conflict(current);
            var target = await ReadCapacityAsync(
                    current.Snapshot.Allocation.Descriptor,
                    current.Snapshot.Allocation.DescriptorLifecycleGeneration,
                    cancellationToken)
                .ConfigureAwait(false);
            var capacity = target.Record.Clone();
            ApplyCapacity(capacity, current.Snapshot.Allocation, activeDelta: -1);
            var result = await provider.WriteAsync(
                    new ZLinkStoreWriteRequest(
                    [
                        new ZLinkStoreCondition.Version(metaKey, current.Version),
                        new ZLinkStoreCondition.Version(
                            OwnerKey(owner.Token.OwnerId),
                            owner.Version),
                        target.Condition
                    ],
                    [
                        new ZLinkStoreMutation.Delete(metaKey),
                        new ZLinkStoreMutation.Delete(payloadKey),
                        new ZLinkStoreMutation.Put(
                            target.Key,
                            Encode(capacity),
                            null)
                    ]),
                    cancellationToken)
                .ConfigureAwait(false);
            return result is ZLinkStoreWriteResult.Applied applied
                ? new ZLinkAuthorityCompareExchangeResult.Deleted(
                    applied.PutVersions[target.Key].Value,
                    applied.StoreNow)
                : Conflict(await ReadAuthorityRecordAsync(key, cancellationToken)
                    .ConfigureAwait(false));
        }

        var put = (ZLinkAuthorityMutation.Put)mutation;
        ValidateAuthorityPayload(put.Payload);
        var changesOwner = put.GenerationTransition
                           == ZLinkAuthorityGenerationTransition.NewOwner;
        var targetOwner = put.TargetOwner
                          ?? new ZLinkLocationOwnerToken(
                              current.Snapshot.OwnerId,
                              current.Snapshot.OwnerLeaseGeneration);
        var liveOwner = await ReadLiveOwnerAsync(targetOwner, cancellationToken)
            .ConfigureAwait(false);
        if (liveOwner is null) return Conflict(current);

        var conditions = new List<ZLinkStoreCondition>
        {
            new ZLinkStoreCondition.Version(metaKey, current.Version),
            new ZLinkStoreCondition.Version(
                OwnerKey(targetOwner.OwnerId),
                liveOwner.Version)
        };
        var mutations = new List<ZLinkStoreMutation>();
        var nextAllocation = current.Snapshot.Allocation;
        RelocationRecordState? relocation = null;
        StoredRecord<RelocationRecordState>? storedRelocation = null;
        StoredCapacity? sourceCapacity = null;
        StoredCapacity? targetCapacity = null;
        if (put.RelocationCapacityFence is { } fence)
        {
            storedRelocation = await ReadRecordAsync<RelocationRecordState>(
                    RelocationKey(fence),
                    cancellationToken)
                .ConfigureAwait(false);
            relocation = storedRelocation?.Record;
            if (relocation is null
                || relocation.Status is not (RelocationStatus.Reserved
                    or RelocationStatus.Prepared)
                || relocation.Request.Key != key
                || relocation.Request.SourceOwner
                != new ZLinkLocationOwnerToken(
                    current.Snapshot.OwnerId,
                    current.Snapshot.OwnerLeaseGeneration)
                || !MatchesSourceAllocation(
                    current.Snapshot.Allocation,
                    relocation.Request))
                return Conflict(current);
            conditions.Add(new ZLinkStoreCondition.Version(
                RelocationKey(fence),
                storedRelocation!.Version));

            if (changesOwner)
            {
                if (relocation.Request.TargetOwner != targetOwner
                    || !await IsEligibleTargetAsync(
                            relocation.Request.TargetDescriptor,
                            relocation.Request.TargetNodeLifecycleGeneration,
                            targetOwner,
                            relocation.Request.ObjectKind,
                            relocation.Request.StableType,
                            cancellationToken)
                        .ConfigureAwait(false))
                    return Conflict(current);
                sourceCapacity = await ReadCapacityAsync(
                        current.Snapshot.Allocation.Descriptor,
                        current.Snapshot.Allocation.DescriptorLifecycleGeneration,
                        cancellationToken)
                    .ConfigureAwait(false);
                targetCapacity = await ReadCapacityAsync(
                        relocation.Request.TargetDescriptor,
                        relocation.Request.TargetNodeLifecycleGeneration,
                        cancellationToken)
                    .ConfigureAwait(false);
                AddCondition(conditions, sourceCapacity.Condition);
                AddCondition(conditions, targetCapacity.Condition);
                var source = sourceCapacity.Record.Clone();
                var target = sourceCapacity.Key == targetCapacity.Key
                    ? source
                    : targetCapacity.Record.Clone();
                ApplyCapacity(source, current.Snapshot.Allocation, activeDelta: -1);
                ApplyCapacity(
                    target,
                    TargetAllocation(relocation.Request),
                    pendingDelta: -1,
                    activeDelta: 1);
                mutations.Add(new ZLinkStoreMutation.Put(
                    sourceCapacity.Key,
                    Encode(source),
                    null));
                if (targetCapacity.Key != sourceCapacity.Key)
                    mutations.Add(new ZLinkStoreMutation.Put(
                        targetCapacity.Key,
                        Encode(target),
                        null));
                relocation = relocation with { Status = RelocationStatus.Committed };
                nextAllocation = new ZLinkPlacementAllocation(
                    ZLinkPlacementAllocationState.Active,
                    relocation.Request.ObjectKind,
                    relocation.Request.StableType,
                    relocation.Request.TargetDescriptor,
                    relocation.Request.TargetNodeLifecycleGeneration,
                    relocation.Request.Capacity);
            }
            else
            {
                relocation = relocation with { Status = RelocationStatus.Prepared };
            }
            mutations.Add(new ZLinkStoreMutation.Put(
                RelocationKey(fence),
                Encode(relocation),
                null));
        }
        else if (changesOwner)
        {
            return Conflict(current);
        }

        var nextAuthorityOwnerGeneration =
            current.Meta.AuthorityOwnerGeneration;
        if (changesOwner)
        {
            var generation = await ReadGenerationAsync(key, cancellationToken)
                .ConfigureAwait(false);
            if (generation.ObjectGeneration != current.Meta.ObjectGeneration
                || generation.AuthorityOwnerGeneration
                != current.Meta.AuthorityOwnerGeneration)
                return Conflict(current);
            nextAuthorityOwnerGeneration = checked(
                generation.AuthorityOwnerGeneration + 1);
            conditions.Add(generation.Condition);
            mutations.Add(new ZLinkStoreMutation.Put(
                GenerationKey(key),
                Encode(new GenerationRecord(
                    generation.ObjectGeneration,
                    nextAuthorityOwnerGeneration)),
                null));
        }
        var meta = current.Meta with
        {
            PayloadSha256 = Sha256(put.Payload),
            AuthorityOwnerGeneration = nextAuthorityOwnerGeneration,
            OwnerId = targetOwner.OwnerId,
            OwnerLeaseGeneration = targetOwner.LeaseGeneration,
            Allocation = nextAllocation
        };
        return await StoreAuthorityAsync(
                current,
                meta,
                put.Payload,
                conditions,
                mutations,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkAuthorityScanResult> ListAuthoritiesAsync(
        string prefix,
        ZLinkAuthorityScanCursor? cursor,
        int limit,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(prefix);
        if (limit is < 1 or > 1000)
            throw new ArgumentOutOfRangeException(nameof(limit));
        var result = await provider.ScanAsync(
                new ZLinkStoreScanRequest(
                    AuthorityMetaPrefix(prefix),
                    cursor is null
                        ? null
                        : new ZLinkStoreScanCursor(cursor.Value.Encoded),
                    limit),
                cancellationToken)
            .ConfigureAwait(false);
        if (result is ZLinkStoreScanResult.Expired)
            return new ZLinkAuthorityScanResult.ScanExpired();
        var page = ((ZLinkStoreScanResult.Page)result).Value;
        var entries = new List<ZLinkAuthorityEntry>(page.Items.Count);
        foreach (var item in page.Items)
        {
            var key = DecodeAuthorityKey(item.Key);
            var stored = await ReadAuthorityRecordAsync(
                    key,
                    item.Value,
                    cancellationToken)
                .ConfigureAwait(false);
            if (stored is not null)
                entries.Add(new ZLinkAuthorityEntry(key, stored.Snapshot));
        }
        return new ZLinkAuthorityScanResult.Page(
            new ZLinkAuthorityPage(
                entries,
                page.NextCursor is null
                    ? null
                    : new ZLinkAuthorityScanCursor(page.NextCursor.Value.Value)));
    }

    public async ValueTask<ZLinkObjectReserveResult> ReserveAsync(
        ZLinkObjectReservationRequest request,
        CancellationToken cancellationToken = default)
    {
        ValidateReservation(request);
        var current = await ReadAuthorityRecordAsync(request.Key, cancellationToken)
            .ConfigureAwait(false);
        if (current is not null)
            return current.Snapshot.Allocation.State
                   == ZLinkPlacementAllocationState.Active
                ? new ZLinkObjectReserveResult.AlreadyExists(current.Snapshot)
                : new ZLinkObjectReserveResult.Conflict(
                    new ZLinkAuthorityReadResult.Found(current.Snapshot));
        var target = await ReadEligibleTargetAsync(
                request.TargetDescriptor,
                request.TargetNodeLifecycleGeneration,
                request.TargetOwner,
                request.ObjectKind,
                request.StableType,
                cancellationToken)
            .ConfigureAwait(false);
        if (target is null)
            return new ZLinkObjectReserveResult.Conflict(
                new ZLinkAuthorityReadResult.Missing(
                    await ReadStoreNowAsync(cancellationToken).ConfigureAwait(false)));

        var capacity = await ReadCapacityAsync(
                request.TargetDescriptor,
                request.TargetNodeLifecycleGeneration,
                cancellationToken)
            .ConfigureAwait(false);
        if (!HasCapacity(target.Descriptor, capacity.Record, request.Capacity))
            return new ZLinkObjectReserveResult.PlacementCapacityExhausted();
        var generation = await ReadGenerationAsync(request.Key, cancellationToken)
            .ConfigureAwait(false);
        if (generation.ObjectGeneration == ulong.MaxValue
            || generation.AuthorityOwnerGeneration == ulong.MaxValue)
            return new ZLinkObjectReserveResult.GenerationExhausted();

        var objectGeneration = generation.ObjectGeneration + 1;
        var authorityOwnerGeneration =
            generation.AuthorityOwnerGeneration + 1;
        var reservationId = Guid.NewGuid().ToString("N");
        var allocation = new ZLinkPlacementAllocation(
            ZLinkPlacementAllocationState.Reserved,
            request.ObjectKind,
            request.StableType,
            request.TargetDescriptor,
            request.TargetNodeLifecycleGeneration,
            request.Capacity);
        var meta = new AuthorityMeta(
            Sha256(request.CreatingPayload),
            objectGeneration,
            authorityOwnerGeneration,
            request.TargetOwner.OwnerId,
            request.TargetOwner.LeaseGeneration,
            allocation,
            new ZLinkReservedObjectCreation(
                reservationId,
                request.CreationIntentReference,
                request.CreationIntentHash.ToArray(),
                request.CreationIntentEncodedSize));
        var reservation = new ReservationRecord(
            request.Key,
            objectGeneration,
            authorityOwnerGeneration,
            reservationId,
            request.TargetDescriptor,
            request.TargetNodeLifecycleGeneration,
            request.TargetOwner,
            ReservationStatus.Reserved);
        var nextCapacity = capacity.Record.Clone();
        ApplyCapacity(nextCapacity, allocation, pendingDelta: 1);
        var metaKey = AuthorityMetaKey(request.Key);
        var result = await provider.WriteAsync(
                new ZLinkStoreWriteRequest(
                [
                    new ZLinkStoreCondition.Missing(metaKey),
                    generation.Condition,
                    target.DescriptorCondition,
                    target.OwnerCondition,
                    capacity.Condition,
                    new ZLinkStoreCondition.Missing(
                        ReservationKey(reservationId))
                ],
                [
                    new ZLinkStoreMutation.Put(metaKey, Encode(meta), null),
                    new ZLinkStoreMutation.Put(
                        AuthorityPayloadKey(request.Key),
                        request.CreatingPayload.ToArray(),
                        null),
                    new ZLinkStoreMutation.Put(
                        GenerationKey(request.Key),
                        Encode(new GenerationRecord(
                            objectGeneration,
                            authorityOwnerGeneration)),
                        null),
                    new ZLinkStoreMutation.Put(
                        ReservationKey(reservationId),
                        Encode(reservation),
                        null),
                    new ZLinkStoreMutation.Put(
                        capacity.Key,
                        Encode(nextCapacity),
                        null)
                ]),
                cancellationToken)
            .ConfigureAwait(false);
        if (result is ZLinkStoreWriteResult.Conflict)
            return await ReserveConflictAsync(request.Key, cancellationToken)
                .ConfigureAwait(false);
        var applied = (ZLinkStoreWriteResult.Applied)result;
        return new ZLinkObjectReserveResult.Reserved(
            new ZLinkObjectReservation(
                request.Key,
                applied.PutVersions[metaKey].Value,
                objectGeneration,
                authorityOwnerGeneration,
                reservationId,
                request.TargetDescriptor,
                request.TargetNodeLifecycleGeneration,
                request.TargetOwner));
    }

    public ValueTask<ZLinkObjectCommitResult> CommitAsync(
        ZLinkObjectReservation reservation,
        ReadOnlyMemory<byte> readyPayload,
        CancellationToken cancellationToken = default) =>
        CompleteCommitAsync(reservation, readyPayload, cancellationToken);

    public async ValueTask<ZLinkObjectCreationCompleteResult>
        CompleteCreationAsync(
            ZLinkObjectReservation reservation,
            ZLinkObjectCreationCompletion completion,
            CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(reservation);
        ArgumentNullException.ThrowIfNull(completion);
        var publication = completion switch
        {
            ZLinkObjectCreationCompletion.Created value => value.Terminal,
            ZLinkObjectCreationCompletion.Rejected value => value.Terminal,
            ZLinkObjectCreationCompletion.Failed value => value.Terminal,
            _ => throw new ArgumentOutOfRangeException(nameof(completion))
        };
        ValidateTerminal(publication);
        var terminalKey = TerminalKey(publication.Operation);
        var existingTerminal = await ReadTerminalAsync(
                publication.Operation,
                cancellationToken)
            .ConfigureAwait(false);
        if (existingTerminal is not null)
            return new ZLinkObjectCreationCompleteResult.AlreadyCompleted(
                existingTerminal.Record);

        var current = await ReadAuthorityRecordAsync(
                reservation.Key,
                cancellationToken)
            .ConfigureAwait(false);
        var storedReservation = await ReadRecordAsync<ReservationRecord>(
                ReservationKey(reservation.ReservationVersion),
                cancellationToken)
            .ConfigureAwait(false);
        if (!MatchesReservation(current, storedReservation, reservation))
            return new ZLinkObjectCreationCompleteResult.Stale();
        if (publication.ExpiresAt <= current!.Snapshot.StoreNow)
            throw new ArgumentOutOfRangeException(
                nameof(completion),
                "The creation terminal must expire after StoreNow.");
        var target = await ReadEligibleTargetAsync(
                reservation.TargetDescriptor,
                reservation.TargetNodeLifecycleGeneration,
                reservation.TargetOwner,
                current!.Snapshot.Allocation.ObjectKind,
                current.Snapshot.Allocation.StableType,
                cancellationToken)
            .ConfigureAwait(false);
        if (target is null)
            return new ZLinkObjectCreationCompleteResult.Stale();
        var capacity = await ReadCapacityAsync(
                reservation.TargetDescriptor,
                reservation.TargetNodeLifecycleGeneration,
                cancellationToken)
            .ConfigureAwait(false);
        var nextCapacity = capacity.Record.Clone();
        ApplyCapacity(
            nextCapacity,
            current.Snapshot.Allocation,
            pendingDelta: -1,
            activeDelta: completion is ZLinkObjectCreationCompletion.Created ? 1 : 0);
        var state = completion switch
        {
            ZLinkObjectCreationCompletion.Created =>
                ZLinkCreationTerminalState.Created,
            ZLinkObjectCreationCompletion.Rejected =>
                ZLinkCreationTerminalState.Rejected,
            _ => ZLinkCreationTerminalState.Failed
        };
        var terminal = new ZLinkCreationTerminalRecord(
            publication.Operation,
            reservation.ReservationVersion,
            current.Snapshot.Allocation.ObjectKind,
            state,
            publication.TerminalEnvelope.ToArray(),
            publication.TerminalEnvelopeSha256.ToArray(),
            publication.ExpiresAt,
            current.Snapshot.StoreNow);
        var terminalMeta = new TerminalMeta(
            terminal with
            {
                TerminalEnvelope = ReadOnlyMemory<byte>.Empty,
                StoreNow = default
            },
            Sha256(publication.TerminalEnvelope));
        var conditions = new List<ZLinkStoreCondition>
        {
            new ZLinkStoreCondition.Version(
                AuthorityMetaKey(reservation.Key),
                current.Version),
            new ZLinkStoreCondition.Version(
                ReservationKey(reservation.ReservationVersion),
                storedReservation!.Version),
            new ZLinkStoreCondition.Missing(terminalKey),
            target.DescriptorCondition,
            target.OwnerCondition,
            capacity.Condition
        };
        var reservationRecord = storedReservation.Record with
        {
            Status = state == ZLinkCreationTerminalState.Created
                ? ReservationStatus.Created
                : state == ZLinkCreationTerminalState.Rejected
                    ? ReservationStatus.Rejected
                    : ReservationStatus.Failed
        };
        var mutations = new List<ZLinkStoreMutation>
        {
            new ZLinkStoreMutation.Put(
                ReservationKey(reservation.ReservationVersion),
                Encode(reservationRecord),
                null),
            new ZLinkStoreMutation.Put(
                terminalKey,
                Encode(terminalMeta),
                publication.ExpiresAt - current.Snapshot.StoreNow),
            new ZLinkStoreMutation.Put(
                TerminalPayloadKey(publication.Operation),
                publication.TerminalEnvelope.ToArray(),
                publication.ExpiresAt - current.Snapshot.StoreNow),
            new ZLinkStoreMutation.Put(capacity.Key, Encode(nextCapacity), null)
        };
        AuthorityMeta? readyMeta = null;
        if (completion is ZLinkObjectCreationCompletion.Created created)
        {
            ValidateAuthorityPayload(created.ReadyPayload);
            readyMeta = current.Meta with
            {
                PayloadSha256 = Sha256(created.ReadyPayload),
                Allocation = current.Snapshot.Allocation with
                {
                    State = ZLinkPlacementAllocationState.Active
                },
                ReservedCreation = null
            };
            mutations.Add(new ZLinkStoreMutation.Put(
                AuthorityMetaKey(reservation.Key),
                Encode(readyMeta),
                null));
            mutations.Add(new ZLinkStoreMutation.Put(
                AuthorityPayloadKey(reservation.Key),
                created.ReadyPayload.ToArray(),
                null));
        }
        else
        {
            mutations.Add(new ZLinkStoreMutation.Delete(
                AuthorityMetaKey(reservation.Key)));
            mutations.Add(new ZLinkStoreMutation.Delete(
                AuthorityPayloadKey(reservation.Key)));
        }
        var result = await provider.WriteAsync(
                new ZLinkStoreWriteRequest(conditions, mutations),
                cancellationToken)
            .ConfigureAwait(false);
        if (result is ZLinkStoreWriteResult.Conflict)
        {
            var raced = await ReadTerminalAsync(
                    publication.Operation,
                    cancellationToken)
                .ConfigureAwait(false);
            return raced is null
                ? new ZLinkObjectCreationCompleteResult.Stale()
                : new ZLinkObjectCreationCompleteResult.AlreadyCompleted(
                    raced.Record);
        }
        var applied = (ZLinkStoreWriteResult.Applied)result;
        terminal = terminal with { StoreNow = applied.StoreNow };
        return completion switch
        {
            ZLinkObjectCreationCompletion.Created => new
                ZLinkObjectCreationCompleteResult.Created(
                    Snapshot(
                        readyMeta!,
                        applied.PutVersions[
                            AuthorityMetaKey(reservation.Key)],
                        applied.StoreNow,
                        ((ZLinkObjectCreationCompletion.Created)completion)
                        .ReadyPayload),
                    terminal),
            ZLinkObjectCreationCompletion.Rejected =>
                new ZLinkObjectCreationCompleteResult.Rejected(terminal),
            _ => new ZLinkObjectCreationCompleteResult.Failed(terminal)
        };
    }

    public async ValueTask<ZLinkCreationTerminalReadResult>
        ReadCreationTerminalAsync(
            ZLinkCreationOperationId operation,
            CancellationToken cancellationToken = default)
    {
        ValidateCreationOperation(operation);
        var terminal = await ReadTerminalAsync(operation, cancellationToken)
            .ConfigureAwait(false);
        return terminal is null
            ? new ZLinkCreationTerminalReadResult.Missing(
                await ReadStoreNowAsync(cancellationToken).ConfigureAwait(false))
            : new ZLinkCreationTerminalReadResult.Found(terminal.Record);
    }

    public async ValueTask<ZLinkObjectAbortResult> AbortAsync(
        ZLinkObjectReservation reservation,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(reservation);
        var current = await ReadAuthorityRecordAsync(
                reservation.Key,
                cancellationToken)
            .ConfigureAwait(false);
        var storedReservation = await ReadRecordAsync<ReservationRecord>(
                ReservationKey(reservation.ReservationVersion),
                cancellationToken)
            .ConfigureAwait(false);
        if (storedReservation?.Record.Status == ReservationStatus.Aborted)
            return new ZLinkObjectAbortResult.AlreadyAborted();
        if (!MatchesReservation(current, storedReservation, reservation))
            return new ZLinkObjectAbortResult.Stale();
        var capacity = await ReadCapacityAsync(
                reservation.TargetDescriptor,
                reservation.TargetNodeLifecycleGeneration,
                cancellationToken)
            .ConfigureAwait(false);
        var nextCapacity = capacity.Record.Clone();
        ApplyCapacity(nextCapacity, current!.Snapshot.Allocation, pendingDelta: -1);
        var result = await provider.WriteAsync(
                new ZLinkStoreWriteRequest(
                [
                    new ZLinkStoreCondition.Version(
                        AuthorityMetaKey(reservation.Key),
                        current.Version),
                    new ZLinkStoreCondition.Version(
                        ReservationKey(reservation.ReservationVersion),
                        storedReservation!.Version),
                    capacity.Condition
                ],
                [
                    new ZLinkStoreMutation.Delete(
                        AuthorityMetaKey(reservation.Key)),
                    new ZLinkStoreMutation.Delete(
                        AuthorityPayloadKey(reservation.Key)),
                    new ZLinkStoreMutation.Put(
                        ReservationKey(reservation.ReservationVersion),
                        Encode(storedReservation.Record with
                        {
                            Status = ReservationStatus.Aborted
                        }),
                        null),
                    new ZLinkStoreMutation.Put(
                        capacity.Key,
                        Encode(nextCapacity),
                        null)
                ]),
                cancellationToken)
            .ConfigureAwait(false);
        return result is ZLinkStoreWriteResult.Applied
            ? new ZLinkObjectAbortResult.Aborted()
            : new ZLinkObjectAbortResult.Stale();
    }

    public async ValueTask<ZLinkRelocationCapacityReserveResult>
        ReserveRelocationCapacityAsync(
            ZLinkRelocationCapacityReservationRequest request,
            CancellationToken cancellationToken = default)
    {
        ValidateRelocationRequest(request);
        var fence = new ZLinkRelocationCapacityFence(
            request.ReservationId.ToString("N"));
        var existing = await ReadRecordAsync<RelocationRecordState>(
                RelocationKey(fence),
                cancellationToken)
            .ConfigureAwait(false);
        if (existing is not null)
            return existing.Record.Request == request
                ? new ZLinkRelocationCapacityReserveResult.AlreadyReserved(fence)
                : new ZLinkRelocationCapacityReserveResult.Conflict(
                    await ReadAuthorityAsync(request.Key, cancellationToken)
                        .ConfigureAwait(false));
        var current = await ReadAuthorityRecordAsync(
                request.Key,
                cancellationToken)
            .ConfigureAwait(false);
        if (current is null
            || current.Version.Value != request.ExpectedStoreVersion
            || current.Snapshot.OwnerId != request.SourceOwner.OwnerId
            || current.Snapshot.OwnerLeaseGeneration
            != request.SourceOwner.LeaseGeneration
            || !MatchesSourceAllocation(current.Snapshot.Allocation, request)
            || await IsAggregateLockedAsync(request.Key, cancellationToken)
                .ConfigureAwait(false))
            return new ZLinkRelocationCapacityReserveResult.Conflict(
                current is null
                    ? new ZLinkAuthorityReadResult.Missing(
                        await ReadStoreNowAsync(cancellationToken)
                            .ConfigureAwait(false))
                    : new ZLinkAuthorityReadResult.Found(current.Snapshot));
        var target = await ReadEligibleTargetAsync(
                request.TargetDescriptor,
                request.TargetNodeLifecycleGeneration,
                request.TargetOwner,
                request.ObjectKind,
                request.StableType,
                cancellationToken)
            .ConfigureAwait(false);
        if (target is null)
            return new ZLinkRelocationCapacityReserveResult.TargetUnavailable();
        var capacity = await ReadCapacityAsync(
                request.TargetDescriptor,
                request.TargetNodeLifecycleGeneration,
                cancellationToken)
            .ConfigureAwait(false);
        if (!HasCapacity(target.Descriptor, capacity.Record, request.Capacity))
            return new ZLinkRelocationCapacityReserveResult
                .PlacementCapacityExhausted();
        var nextCapacity = capacity.Record.Clone();
        ApplyCapacity(
            nextCapacity,
            TargetAllocation(request),
            pendingDelta: 1);
        var result = await provider.WriteAsync(
                new ZLinkStoreWriteRequest(
                [
                    new ZLinkStoreCondition.Version(
                        AuthorityMetaKey(request.Key),
                        current.Version),
                    new ZLinkStoreCondition.Missing(RelocationKey(fence)),
                    target.DescriptorCondition,
                    target.OwnerCondition,
                    capacity.Condition
                ],
                [
                    new ZLinkStoreMutation.Put(
                        RelocationKey(fence),
                        Encode(new RelocationRecordState(
                            request,
                            RelocationStatus.Reserved)),
                        null),
                    new ZLinkStoreMutation.Put(
                        capacity.Key,
                        Encode(nextCapacity),
                        null)
                ]),
                cancellationToken)
            .ConfigureAwait(false);
        return result is ZLinkStoreWriteResult.Applied
            ? new ZLinkRelocationCapacityReserveResult.Reserved(fence)
            : new ZLinkRelocationCapacityReserveResult.Conflict(
                await ReadAuthorityAsync(request.Key, cancellationToken)
                    .ConfigureAwait(false));
    }

    public async ValueTask<ZLinkRelocationCapacityAbortResult>
        AbortRelocationCapacityAsync(
            ZLinkRelocationCapacityFence fence,
            CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(fence.Value);
        var stored = await ReadRecordAsync<RelocationRecordState>(
                RelocationKey(fence),
                cancellationToken)
            .ConfigureAwait(false);
        if (stored is null) return ZLinkRelocationCapacityAbortResult.Stale;
        if (stored.Record.Status == RelocationStatus.Aborted)
            return ZLinkRelocationCapacityAbortResult.AlreadyAborted;
        if (stored.Record.Status == RelocationStatus.Committed)
            return ZLinkRelocationCapacityAbortResult.AlreadyCommitted;
        if (stored.Record.Status == RelocationStatus.Prepared)
            return ZLinkRelocationCapacityAbortResult.Stale;
        var request = stored.Record.Request;
        var capacity = await ReadCapacityAsync(
                request.TargetDescriptor,
                request.TargetNodeLifecycleGeneration,
                cancellationToken)
            .ConfigureAwait(false);
        var nextCapacity = capacity.Record.Clone();
        ApplyCapacity(
            nextCapacity,
            TargetAllocation(request),
            pendingDelta: -1);
        var result = await provider.WriteAsync(
                new ZLinkStoreWriteRequest(
                [
                    new ZLinkStoreCondition.Version(
                        RelocationKey(fence),
                        stored.Version),
                    capacity.Condition
                ],
                [
                    new ZLinkStoreMutation.Put(
                        RelocationKey(fence),
                        Encode(stored.Record with
                        {
                            Status = RelocationStatus.Aborted
                        }),
                        null),
                    new ZLinkStoreMutation.Put(
                        capacity.Key,
                        Encode(nextCapacity),
                        null)
                ]),
                cancellationToken)
            .ConfigureAwait(false);
        return result is ZLinkStoreWriteResult.Applied
            ? ZLinkRelocationCapacityAbortResult.Aborted
            : ZLinkRelocationCapacityAbortResult.Stale;
    }

    public async ValueTask<ZLinkAggregatePrepareResult>
        PrepareAggregateAsync(
            ZLinkAggregatePrepareRequest request,
            CancellationToken cancellationToken = default)
    {
        ValidateAggregate(request);
        var fence = new ZLinkAggregateFence(
            request.AggregateId,
            request.AggregateGeneration);
        var key = AggregateKey(fence);
        var existing = await ReadRecordAsync<AggregateRecord>(
                key,
                cancellationToken)
            .ConfigureAwait(false);
        if (existing is not null)
            return existing.Record.Status == AggregateStatus.Prepared
                   && AggregateRequestsEqual(existing.Record.Request, request)
                ? new ZLinkAggregatePrepareResult.AlreadyPrepared(fence)
                : new ZLinkAggregatePrepareResult.Conflict();

        var target = await ReadEligibleTargetAsync(
                request.TargetDescriptor,
                request.TargetDescriptorLifecycleGeneration,
                request.TargetOwner,
                null,
                null,
                cancellationToken)
            .ConfigureAwait(false);
        if (target is null)
            return new ZLinkAggregatePrepareResult.Conflict();
        var authorities = new List<StoredAuthority>(
            request.Participants.Count);
        foreach (var participant in request.Participants)
        {
            var authority = await ReadAuthorityRecordAsync(
                    participant.Key,
                    cancellationToken)
                .ConfigureAwait(false);
            if (authority is null
                || authority.Version.Value != participant.ExpectedStoreVersion
                || await IsAggregateLockedAsync(
                        participant.Key,
                        cancellationToken)
                    .ConfigureAwait(false))
                return new ZLinkAggregatePrepareResult.Conflict();
            if (participant.OwnerTransition
                    == ZLinkAuthorityGenerationTransition.NewOwner
                && !IsEligible(target.Descriptor, authority.Snapshot.Allocation))
                return new ZLinkAggregatePrepareResult.Conflict();
            authorities.Add(authority);
        }
        var capacity = await ReadCapacityAsync(
                request.TargetDescriptor,
                request.TargetDescriptorLifecycleGeneration,
                cancellationToken)
            .ConfigureAwait(false);
        if (!HasCapacity(target.Descriptor, capacity.Record, request.Capacity))
            return new ZLinkAggregatePrepareResult.Conflict();
        var nextCapacity = capacity.Record.Clone();
        ApplyCapacityVector(
            nextCapacity,
            request.Capacity,
            pendingDelta: 1);
        var conditions = new List<ZLinkStoreCondition>
        {
            new ZLinkStoreCondition.Missing(key),
            target.DescriptorCondition,
            target.OwnerCondition,
            capacity.Condition
        };
        var mutations = new List<ZLinkStoreMutation>
        {
            new ZLinkStoreMutation.Put(
                key,
                Encode(new AggregateRecord(request, AggregateStatus.Prepared)),
                null),
            new ZLinkStoreMutation.Put(
                capacity.Key,
                Encode(nextCapacity),
                null)
        };
        for (var index = 0; index < request.Participants.Count; index++)
        {
            var participant = request.Participants[index];
            conditions.Add(new ZLinkStoreCondition.Version(
                AuthorityMetaKey(participant.Key),
                authorities[index].Version));
            conditions.Add(new ZLinkStoreCondition.Missing(
                AggregateLockKey(participant.Key)));
            mutations.Add(new ZLinkStoreMutation.Put(
                AggregateLockKey(participant.Key),
                Encode(fence),
                null));
        }
        var result = await provider.WriteAsync(
                new ZLinkStoreWriteRequest(conditions, mutations),
                cancellationToken)
            .ConfigureAwait(false);
        return result is ZLinkStoreWriteResult.Applied
            ? new ZLinkAggregatePrepareResult.Prepared(fence)
            : new ZLinkAggregatePrepareResult.Conflict();
    }

    public async ValueTask<ZLinkAggregateCommitResult>
        CommitAggregateAsync(
            ZLinkAggregateFence fence,
            CancellationToken cancellationToken = default)
    {
        var key = AggregateKey(fence);
        var aggregate = await ReadRecordAsync<AggregateRecord>(
                key,
                cancellationToken)
            .ConfigureAwait(false);
        if (aggregate is null
            || aggregate.Record.Status == AggregateStatus.Aborted)
            return ZLinkAggregateCommitResult.Stale;
        if (aggregate.Record.Status == AggregateStatus.Committed)
            return ZLinkAggregateCommitResult.AlreadyCommitted;
        var request = aggregate.Record.Request;
        var target = await ReadEligibleTargetAsync(
                request.TargetDescriptor,
                request.TargetDescriptorLifecycleGeneration,
                request.TargetOwner,
                null,
                null,
                cancellationToken)
            .ConfigureAwait(false);
        if (target is null) return ZLinkAggregateCommitResult.Stale;
        var authorities = new List<StoredAuthority>(request.Participants.Count);
        foreach (var participant in request.Participants)
        {
            var authority = await ReadAuthorityRecordAsync(
                    participant.Key,
                    cancellationToken)
                .ConfigureAwait(false);
            var locked = await ReadRecordAsync<ZLinkAggregateFence>(
                    AggregateLockKey(participant.Key),
                    cancellationToken)
                .ConfigureAwait(false);
            if (authority is null
                || authority.Version.Value != participant.ExpectedStoreVersion
                || locked?.Record != fence)
                return ZLinkAggregateCommitResult.Stale;
            authorities.Add(authority);
        }
        var capacity = await ReadCapacityAsync(
                request.TargetDescriptor,
                request.TargetDescriptorLifecycleGeneration,
                cancellationToken)
            .ConfigureAwait(false);
        var capacityRecords = new Dictionary<ZLinkStoreKey, StoredCapacity>();
        capacityRecords[capacity.Key] = capacity;
        foreach (var authority in authorities)
        {
            var source = await ReadCapacityAsync(
                    authority.Snapshot.Allocation.Descriptor,
                    authority.Snapshot.Allocation
                        .DescriptorLifecycleGeneration,
                    cancellationToken)
                .ConfigureAwait(false);
            capacityRecords[source.Key] = source;
        }
        var updatedCapacity = capacityRecords.ToDictionary(
            static pair => pair.Key,
            static pair => pair.Value.Record.Clone());
        ApplyCapacityVector(
            updatedCapacity[capacity.Key],
            request.Capacity,
            pendingDelta: -1);

        var conditions = new List<ZLinkStoreCondition>
        {
            new ZLinkStoreCondition.Version(key, aggregate.Version),
            target.DescriptorCondition,
            target.OwnerCondition
        };
        var mutations = new List<ZLinkStoreMutation>();
        foreach (var pair in capacityRecords)
        {
            AddCondition(conditions, pair.Value.Condition);
        }
        for (var index = 0; index < request.Participants.Count; index++)
        {
            var participant = request.Participants[index];
            var authority = authorities[index];
            conditions.Add(new ZLinkStoreCondition.Version(
                AuthorityMetaKey(participant.Key),
                authority.Version));
            var lockRecord = await provider.ReadAsync(
                    AggregateLockKey(participant.Key),
                    cancellationToken)
                .ConfigureAwait(false);
            if (lockRecord is not ZLinkStoreReadResult.Found lockFound)
                return ZLinkAggregateCommitResult.Stale;
            conditions.Add(new ZLinkStoreCondition.Version(
                AggregateLockKey(participant.Key),
                lockFound.Value.Version));
            var changesOwner = participant.OwnerTransition
                               == ZLinkAuthorityGenerationTransition.NewOwner;
            var allocation = authority.Snapshot.Allocation;
            var nextAuthorityOwnerGeneration =
                authority.Meta.AuthorityOwnerGeneration;
            if (changesOwner)
            {
                var generation = await ReadGenerationAsync(
                        participant.Key,
                        cancellationToken)
                    .ConfigureAwait(false);
                if (generation.ObjectGeneration
                        != authority.Meta.ObjectGeneration
                    || generation.AuthorityOwnerGeneration
                    != authority.Meta.AuthorityOwnerGeneration)
                    return ZLinkAggregateCommitResult.Stale;
                nextAuthorityOwnerGeneration = checked(
                    generation.AuthorityOwnerGeneration + 1);
                conditions.Add(generation.Condition);
                mutations.Add(new ZLinkStoreMutation.Put(
                    GenerationKey(participant.Key),
                    Encode(new GenerationRecord(
                        generation.ObjectGeneration,
                        nextAuthorityOwnerGeneration)),
                    null));
                ApplyCapacity(
                    updatedCapacity[CapacityKey(
                        allocation.Descriptor,
                        allocation.DescriptorLifecycleGeneration)],
                    allocation,
                    activeDelta: -1);
                var moved = allocation with
                {
                    Descriptor = request.TargetDescriptor,
                    DescriptorLifecycleGeneration =
                        request.TargetDescriptorLifecycleGeneration
                };
                ApplyCapacity(
                    updatedCapacity[capacity.Key],
                    moved,
                    activeDelta: 1);
                allocation = moved;
            }
            var meta = authority.Meta with
            {
                PayloadSha256 = Sha256(participant.AuthorityPayload),
                AuthorityOwnerGeneration = nextAuthorityOwnerGeneration,
                OwnerId = changesOwner
                    ? request.TargetOwner.OwnerId
                    : authority.Meta.OwnerId,
                OwnerLeaseGeneration = changesOwner
                    ? request.TargetOwner.LeaseGeneration
                    : authority.Meta.OwnerLeaseGeneration,
                Allocation = allocation
            };
            mutations.Add(new ZLinkStoreMutation.Put(
                AuthorityMetaKey(participant.Key),
                Encode(meta),
                null));
            mutations.Add(new ZLinkStoreMutation.Put(
                AuthorityPayloadKey(participant.Key),
                participant.AuthorityPayload.ToArray(),
                null));
            mutations.Add(new ZLinkStoreMutation.Delete(
                AggregateLockKey(participant.Key)));
        }
        foreach (var pair in updatedCapacity)
            mutations.Add(new ZLinkStoreMutation.Put(
                pair.Key,
                Encode(pair.Value),
                null));
        mutations.Add(new ZLinkStoreMutation.Put(
            key,
            Encode(aggregate.Record with { Status = AggregateStatus.Committed }),
            null));
        var result = await provider.WriteAsync(
                new ZLinkStoreWriteRequest(conditions, mutations),
                cancellationToken)
            .ConfigureAwait(false);
        return result is ZLinkStoreWriteResult.Applied
            ? ZLinkAggregateCommitResult.Committed
            : ZLinkAggregateCommitResult.Stale;
    }

    public async ValueTask<ZLinkAggregateAbortResult>
        AbortAggregateAsync(
            ZLinkAggregateFence fence,
            CancellationToken cancellationToken = default)
    {
        var key = AggregateKey(fence);
        var aggregate = await ReadRecordAsync<AggregateRecord>(
                key,
                cancellationToken)
            .ConfigureAwait(false);
        if (aggregate is null) return ZLinkAggregateAbortResult.Stale;
        if (aggregate.Record.Status == AggregateStatus.Aborted)
            return ZLinkAggregateAbortResult.AlreadyAborted;
        if (aggregate.Record.Status == AggregateStatus.Committed)
            return ZLinkAggregateAbortResult.Stale;
        var request = aggregate.Record.Request;
        var capacity = await ReadCapacityAsync(
                request.TargetDescriptor,
                request.TargetDescriptorLifecycleGeneration,
                cancellationToken)
            .ConfigureAwait(false);
        var nextCapacity = capacity.Record.Clone();
        ApplyCapacityVector(
            nextCapacity,
            request.Capacity,
            pendingDelta: -1);
        var conditions = new List<ZLinkStoreCondition>
        {
            new ZLinkStoreCondition.Version(key, aggregate.Version),
            capacity.Condition
        };
        var mutations = new List<ZLinkStoreMutation>
        {
            new ZLinkStoreMutation.Put(
                key,
                Encode(aggregate.Record with { Status = AggregateStatus.Aborted }),
                null),
            new ZLinkStoreMutation.Put(
                capacity.Key,
                Encode(nextCapacity),
                null)
        };
        foreach (var participant in request.Participants)
        {
            var lockResult = await provider.ReadAsync(
                    AggregateLockKey(participant.Key),
                    cancellationToken)
                .ConfigureAwait(false);
            if (lockResult is not ZLinkStoreReadResult.Found found)
                return ZLinkAggregateAbortResult.Stale;
            conditions.Add(new ZLinkStoreCondition.Version(
                AggregateLockKey(participant.Key),
                found.Value.Version));
            mutations.Add(new ZLinkStoreMutation.Delete(
                AggregateLockKey(participant.Key)));
        }
        var result = await provider.WriteAsync(
                new ZLinkStoreWriteRequest(conditions, mutations),
                cancellationToken)
            .ConfigureAwait(false);
        return result is ZLinkStoreWriteResult.Applied
            ? ZLinkAggregateAbortResult.Aborted
            : ZLinkAggregateAbortResult.Stale;
    }

    private async ValueTask<ZLinkObjectCommitResult> CompleteCommitAsync(
        ZLinkObjectReservation reservation,
        ReadOnlyMemory<byte> readyPayload,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(reservation);
        ValidateAuthorityPayload(readyPayload);
        var current = await ReadAuthorityRecordAsync(
                reservation.Key,
                cancellationToken)
            .ConfigureAwait(false);
        var storedReservation = await ReadRecordAsync<ReservationRecord>(
                ReservationKey(reservation.ReservationVersion),
                cancellationToken)
            .ConfigureAwait(false);
        if (storedReservation?.Record.Status == ReservationStatus.Created)
            return current is null
                ? new ZLinkObjectCommitResult.Stale()
                : new ZLinkObjectCommitResult.AlreadyCommitted(current.Snapshot);
        if (!MatchesReservation(current, storedReservation, reservation))
            return new ZLinkObjectCommitResult.Stale();
        var target = await ReadEligibleTargetAsync(
                reservation.TargetDescriptor,
                reservation.TargetNodeLifecycleGeneration,
                reservation.TargetOwner,
                current!.Snapshot.Allocation.ObjectKind,
                current.Snapshot.Allocation.StableType,
                cancellationToken)
            .ConfigureAwait(false);
        if (target is null) return new ZLinkObjectCommitResult.Stale();
        var capacity = await ReadCapacityAsync(
                reservation.TargetDescriptor,
                reservation.TargetNodeLifecycleGeneration,
                cancellationToken)
            .ConfigureAwait(false);
        var nextCapacity = capacity.Record.Clone();
        ApplyCapacity(
            nextCapacity,
            current.Snapshot.Allocation,
            pendingDelta: -1,
            activeDelta: 1);
        var meta = current.Meta with
        {
            PayloadSha256 = Sha256(readyPayload),
            Allocation = current.Snapshot.Allocation with
            {
                State = ZLinkPlacementAllocationState.Active
            },
            ReservedCreation = null
        };
        var result = await provider.WriteAsync(
                new ZLinkStoreWriteRequest(
                [
                    new ZLinkStoreCondition.Version(
                        AuthorityMetaKey(reservation.Key),
                        current.Version),
                    new ZLinkStoreCondition.Version(
                        ReservationKey(reservation.ReservationVersion),
                        storedReservation!.Version),
                    target.DescriptorCondition,
                    target.OwnerCondition,
                    capacity.Condition
                ],
                [
                    new ZLinkStoreMutation.Put(
                        AuthorityMetaKey(reservation.Key),
                        Encode(meta),
                        null),
                    new ZLinkStoreMutation.Put(
                        AuthorityPayloadKey(reservation.Key),
                        readyPayload.ToArray(),
                        null),
                    new ZLinkStoreMutation.Put(
                        ReservationKey(reservation.ReservationVersion),
                        Encode(storedReservation.Record with
                        {
                            Status = ReservationStatus.Created
                        }),
                        null),
                    new ZLinkStoreMutation.Put(
                        capacity.Key,
                        Encode(nextCapacity),
                        null)
                ]),
                cancellationToken)
            .ConfigureAwait(false);
        if (result is not ZLinkStoreWriteResult.Applied applied)
            return new ZLinkObjectCommitResult.Stale();
        return new ZLinkObjectCommitResult.Committed(
            Snapshot(
                meta,
                applied.PutVersions[AuthorityMetaKey(reservation.Key)],
                applied.StoreNow,
                readyPayload));
    }

    private async ValueTask<ZLinkAuthorityCompareExchangeResult> StoreAuthorityAsync(
        StoredAuthority current,
        AuthorityMeta meta,
        ReadOnlyMemory<byte> payload,
        IReadOnlyList<ZLinkStoreCondition> conditions,
        IReadOnlyList<ZLinkStoreMutation> extraMutations,
        CancellationToken cancellationToken)
    {
        var metaKey = AuthorityMetaKey(current.Key);
        var mutations = new List<ZLinkStoreMutation>(extraMutations)
        {
            new ZLinkStoreMutation.Put(metaKey, Encode(meta), null),
            new ZLinkStoreMutation.Put(
                AuthorityPayloadKey(current.Key),
                payload.ToArray(),
                null)
        };
        var result = await provider.WriteAsync(
                new ZLinkStoreWriteRequest(conditions, mutations),
                cancellationToken)
            .ConfigureAwait(false);
        if (result is not ZLinkStoreWriteResult.Applied applied)
            return Conflict(await ReadAuthorityRecordAsync(
                    current.Key,
                    cancellationToken)
                .ConfigureAwait(false));
        return new ZLinkAuthorityCompareExchangeResult.Stored(
            Snapshot(
                meta,
                applied.PutVersions[metaKey],
                applied.StoreNow,
                payload));
    }

    private async ValueTask<StoredAuthority?> ReadAuthorityRecordAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken) =>
        await ReadAuthorityRecordAsync(key, null, cancellationToken)
            .ConfigureAwait(false);

    private async ValueTask<StoredAuthority?> ReadAuthorityRecordAsync(
        ZLinkAuthorityKey key,
        ZLinkStoreValue? knownMeta,
        CancellationToken cancellationToken)
    {
        var metaKey = AuthorityMetaKey(key);
        for (var attempt = 0; attempt < 8; attempt++)
        {
            var first = knownMeta is null
                ? await provider.ReadAsync(metaKey, cancellationToken)
                    .ConfigureAwait(false)
                : new ZLinkStoreReadResult.Found(knownMeta);
            knownMeta = null;
            if (first is ZLinkStoreReadResult.Missing) return null;
            var found = ((ZLinkStoreReadResult.Found)first).Value;
            var meta = Decode<AuthorityMeta>(found.Bytes);
            var payload = await provider.ReadAsync(
                    AuthorityPayloadKey(key),
                    cancellationToken)
                .ConfigureAwait(false);
            if (payload is not ZLinkStoreReadResult.Found payloadFound)
                throw new InvalidDataException(
                    $"Authority '{key.Value}' has no payload.");
            var verify = await provider.ReadAsync(metaKey, cancellationToken)
                .ConfigureAwait(false);
            if (verify is not ZLinkStoreReadResult.Found verified
                || verified.Value.Version != found.Version)
                continue;
            if (!CryptographicOperations.FixedTimeEquals(
                    meta.PayloadSha256,
                    Sha256(payloadFound.Value.Bytes)))
                throw new InvalidDataException(
                    $"Authority '{key.Value}' payload checksum is invalid.");
            return new StoredAuthority(
                key,
                meta,
                found.Version,
                Snapshot(
                    meta,
                    found.Version,
                    verified.Value.StoreNow,
                    payloadFound.Value.Bytes));
        }
        throw new IOException(
            $"Authority '{key.Value}' changed continuously while it was read.");
    }

    private async ValueTask<StoredTarget?> ReadEligibleTargetAsync(
        ZLinkMeshNodeDescriptorKey key,
        ulong lifecycleGeneration,
        ZLinkLocationOwnerToken owner,
        ZLinkPlacementObjectKind? objectKind,
        string? stableType,
        CancellationToken cancellationToken)
    {
        var descriptorKey = MeshKey(key.MeshName, key.Rid);
        var descriptorRead = await provider.ReadAsync(
                descriptorKey,
                cancellationToken)
            .ConfigureAwait(false);
        if (descriptorRead is not ZLinkStoreReadResult.Found descriptorFound)
            return null;
        var descriptor = Decode<MeshRecord>(descriptorFound.Value.Bytes).Descriptor;
        var ownerRead = await ReadLiveOwnerAsync(owner, cancellationToken)
            .ConfigureAwait(false);
        if (ownerRead is null
            || descriptor.MeshName != key.MeshName
            || descriptor.Rid != key.Rid
            || descriptor.LifecycleGeneration != lifecycleGeneration
            || descriptor.OwnerId != owner.OwnerId
            || descriptor.LeaseGeneration != owner.LeaseGeneration
            || descriptor.ObjectRole != ZLinkMeshNodeObjectRole.Server
            || descriptor.State != ZLinkFrameworkRuntimeState.Serving
            || descriptor.PlacementWeight <= 0)
            return null;
        if (objectKind is { } kind
            && !descriptor.ObjectCapabilities.Any(capability =>
                capability.ObjectKind == kind
                && string.Equals(
                    capability.StableType,
                    stableType,
                    StringComparison.Ordinal)))
            return null;
        return new StoredTarget(
            descriptor,
            new ZLinkStoreCondition.Version(
                descriptorKey,
                descriptorFound.Value.Version),
            new ZLinkStoreCondition.Version(
                OwnerKey(owner.OwnerId),
                ownerRead.Version));
    }

    private async ValueTask<bool> IsEligibleTargetAsync(
        ZLinkMeshNodeDescriptorKey key,
        ulong lifecycleGeneration,
        ZLinkLocationOwnerToken owner,
        ZLinkPlacementObjectKind objectKind,
        string stableType,
        CancellationToken cancellationToken) =>
        await ReadEligibleTargetAsync(
                key,
                lifecycleGeneration,
                owner,
                objectKind,
                stableType,
                cancellationToken)
            .ConfigureAwait(false) is not null;

    private async ValueTask<StoredOwner?> ReadLiveOwnerAsync(
        ZLinkLocationOwnerToken token,
        CancellationToken cancellationToken)
    {
        var read = await provider.ReadAsync(
                OwnerKey(token.OwnerId),
                cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkStoreReadResult.Found found) return null;
        var record = Decode<OwnerRecord>(found.Value.Bytes);
        return record.OwnerId == token.OwnerId
               && record.LeaseGeneration == token.LeaseGeneration
            ? new StoredOwner(token, found.Value.Version)
            : null;
    }

    private async ValueTask<StoredCapacity> ReadCapacityAsync(
        ZLinkMeshNodeDescriptorKey descriptor,
        ulong lifecycleGeneration,
        CancellationToken cancellationToken)
    {
        var key = CapacityKey(descriptor, lifecycleGeneration);
        var read = await provider.ReadAsync(key, cancellationToken)
            .ConfigureAwait(false);
        return read switch
        {
            ZLinkStoreReadResult.Missing => new StoredCapacity(
                key,
                new CapacityRecord(),
                new ZLinkStoreCondition.Missing(key)),
            ZLinkStoreReadResult.Found found => new StoredCapacity(
                key,
                Decode<CapacityRecord>(found.Value.Bytes),
                new ZLinkStoreCondition.Version(key, found.Value.Version)),
            _ => throw new InvalidOperationException()
        };
    }

    private async ValueTask<GenerationState> ReadGenerationAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken)
    {
        var storeKey = GenerationKey(key);
        var read = await provider.ReadAsync(storeKey, cancellationToken)
            .ConfigureAwait(false);
        return read switch
        {
            ZLinkStoreReadResult.Missing => new GenerationState(
                0,
                0,
                new ZLinkStoreCondition.Missing(storeKey)),
            ZLinkStoreReadResult.Found found => new GenerationState(
                Decode<GenerationRecord>(found.Value.Bytes).ObjectGeneration,
                Decode<GenerationRecord>(found.Value.Bytes)
                    .AuthorityOwnerGeneration,
                new ZLinkStoreCondition.Version(
                    storeKey,
                    found.Value.Version)),
            _ => throw new InvalidOperationException()
        };
    }

    private async ValueTask<StoredTerminal?> ReadTerminalAsync(
        ZLinkCreationOperationId operation,
        CancellationToken cancellationToken)
    {
        var key = TerminalKey(operation);
        for (var attempt = 0; attempt < 8; attempt++)
        {
            var metaRead = await provider.ReadAsync(key, cancellationToken)
                .ConfigureAwait(false);
            if (metaRead is not ZLinkStoreReadResult.Found metaFound)
                return null;
            var meta = Decode<TerminalMeta>(metaFound.Value.Bytes);
            var payloadRead = await provider.ReadAsync(
                    TerminalPayloadKey(operation),
                    cancellationToken)
                .ConfigureAwait(false);
            if (payloadRead is not ZLinkStoreReadResult.Found payloadFound)
                return null;
            var verify = await provider.ReadAsync(key, cancellationToken)
                .ConfigureAwait(false);
            if (verify is not ZLinkStoreReadResult.Found verified
                || verified.Value.Version != metaFound.Value.Version)
                continue;
            if (!CryptographicOperations.FixedTimeEquals(
                    meta.PayloadSha256,
                    Sha256(payloadFound.Value.Bytes)))
                throw new InvalidDataException(
                    "The creation terminal payload checksum is invalid.");
            return new StoredTerminal(
                meta.Record with
                {
                    TerminalEnvelope = payloadFound.Value.Bytes,
                    StoreNow = verified.Value.StoreNow
                },
                metaFound.Value.Version);
        }
        throw new IOException(
            "The creation terminal changed continuously while it was read.");
    }

    private async ValueTask<StoredRecord<T>?> ReadRecordAsync<T>(
        ZLinkStoreKey key,
        CancellationToken cancellationToken)
    {
        var read = await provider.ReadAsync(key, cancellationToken)
            .ConfigureAwait(false);
        return read is ZLinkStoreReadResult.Found found
            ? new StoredRecord<T>(
                Decode<T>(found.Value.Bytes),
                found.Value.Version,
                found.Value.StoreNow)
            : null;
    }

    private async ValueTask<bool> IsAggregateLockedAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken) =>
        await provider.ReadAsync(
                AggregateLockKey(key),
                cancellationToken)
            .ConfigureAwait(false) is ZLinkStoreReadResult.Found;

    private async ValueTask<DateTimeOffset> ReadStoreNowAsync(
        CancellationToken cancellationToken)
    {
        var read = await provider.ReadAsync(
                Key(Prefix + "clock"),
                cancellationToken)
            .ConfigureAwait(false);
        return read switch
        {
            ZLinkStoreReadResult.Missing missing => missing.StoreNow,
            ZLinkStoreReadResult.Found found => found.Value.StoreNow,
            _ => throw new InvalidOperationException()
        };
    }

    private async ValueTask<ZLinkObjectReserveResult> ReserveConflictAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken)
    {
        var current = await ReadAuthorityRecordAsync(key, cancellationToken)
            .ConfigureAwait(false);
        return current is null
            ? new ZLinkObjectReserveResult.Conflict(
                new ZLinkAuthorityReadResult.Missing(
                    await ReadStoreNowAsync(cancellationToken)
                        .ConfigureAwait(false)))
            : current.Snapshot.Allocation.State
              == ZLinkPlacementAllocationState.Active
                ? new ZLinkObjectReserveResult.AlreadyExists(current.Snapshot)
                : new ZLinkObjectReserveResult.Conflict(
                    new ZLinkAuthorityReadResult.Found(current.Snapshot));
    }

    private static bool MatchesReservation(
        StoredAuthority? current,
        StoredRecord<ReservationRecord>? stored,
        ZLinkObjectReservation reservation) =>
        current is not null
        && stored is not null
        && stored.Record.Status == ReservationStatus.Reserved
        && stored.Record.Key == reservation.Key
        && stored.Record.ObjectGeneration == reservation.ObjectGeneration
        && stored.Record.AuthorityOwnerGeneration
        == reservation.AuthorityOwnerGeneration
        && stored.Record.ReservationId == reservation.ReservationVersion
        && stored.Record.TargetDescriptor == reservation.TargetDescriptor
        && stored.Record.TargetLifecycleGeneration
        == reservation.TargetNodeLifecycleGeneration
        && stored.Record.TargetOwner == reservation.TargetOwner
        && current.Version.Value == reservation.StoreVersion
        && current.Snapshot.Allocation.State
        == ZLinkPlacementAllocationState.Reserved;

    private static ZLinkAuthorityCompareExchangeResult Conflict(
        StoredAuthority? current) =>
        new ZLinkAuthorityCompareExchangeResult.Conflict(
            current is null
                ? new ZLinkAuthorityReadResult.Missing(DateTimeOffset.UtcNow)
                : new ZLinkAuthorityReadResult.Found(current.Snapshot));

    private static ZLinkAuthoritySnapshot Snapshot(
        AuthorityMeta meta,
        ZLinkStoreVersion version,
        DateTimeOffset now,
        ReadOnlyMemory<byte> payload) =>
        new(
            version.Value,
            payload.ToArray(),
            meta.ObjectGeneration,
            meta.AuthorityOwnerGeneration,
            meta.OwnerId,
            meta.OwnerLeaseGeneration,
            meta.Allocation,
            meta.ReservedCreation,
            now);

    private static bool MatchesSourceAllocation(
        ZLinkPlacementAllocation allocation,
        ZLinkRelocationCapacityReservationRequest request) =>
        allocation.State == ZLinkPlacementAllocationState.Active
        && allocation.ObjectKind == request.ObjectKind
        && allocation.StableType == request.StableType
        && allocation.Descriptor == request.SourceDescriptor
        && allocation.DescriptorLifecycleGeneration
        == request.SourceNodeLifecycleGeneration
        && allocation.Capacity == request.Capacity;

    private static ZLinkPlacementAllocation TargetAllocation(
        ZLinkRelocationCapacityReservationRequest request) =>
        new(
            ZLinkPlacementAllocationState.Reserved,
            request.ObjectKind,
            request.StableType,
            request.TargetDescriptor,
            request.TargetNodeLifecycleGeneration,
            request.Capacity);

    private static bool IsEligible(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkPlacementAllocation allocation) =>
        descriptor.ObjectCapabilities.Any(capability =>
            capability.ObjectKind == allocation.ObjectKind
            && capability.StableType == allocation.StableType);

    private static bool HasCapacity(
        ZLinkMeshNodeDescriptor descriptor,
        CapacityRecord usage,
        ZLinkCapacityVector requested)
    {
        if (descriptor.Capacity.Actors.Limit > 0
            && usage.ActorsActive + usage.ActorsPending + requested.Actors
            > descriptor.Capacity.Actors.Limit)
            return false;
        if (descriptor.Capacity.Spots.Limit > 0
            && usage.SpotsActive + usage.SpotsPending + requested.Spots
            > descriptor.Capacity.Spots.Limit)
            return false;
        if (requested.SpotType is not { } spotType)
            return requested.Spots == 0;
        var limit = descriptor.ObjectCapabilities.SingleOrDefault(
            capability => capability.ObjectKind == spotType.ObjectKind
                          && capability.StableType == spotType.StableType)
            ?.Limit;
        if (limit is null) return false;
        var count = usage.SpotTypes.GetValueOrDefault(
            CapacityTypeKey(spotType.ObjectKind, spotType.StableType));
        return requested.Spots == spotType.Count
               && (limit == 0
                   || count.Active + count.Pending + spotType.Count <= limit);
    }

    private static void ApplyCapacity(
        CapacityRecord record,
        ZLinkPlacementAllocation allocation,
        int pendingDelta = 0,
        int activeDelta = 0) =>
        ApplyCapacityVector(
            record,
            allocation.Capacity,
            pendingDelta,
            activeDelta);

    private static void ApplyCapacityVector(
        CapacityRecord record,
        ZLinkCapacityVector vector,
        int pendingDelta = 0,
        int activeDelta = 0)
    {
        record.ActorsPending = checked(
            record.ActorsPending + vector.Actors * pendingDelta);
        record.ActorsActive = checked(
            record.ActorsActive + vector.Actors * activeDelta);
        record.SpotsPending = checked(
            record.SpotsPending + vector.Spots * pendingDelta);
        record.SpotsActive = checked(
            record.SpotsActive + vector.Spots * activeDelta);
        if (vector.SpotType is not { } spotType) return;
        var key = CapacityTypeKey(
            spotType.ObjectKind,
            spotType.StableType);
        var current = record.SpotTypes.GetValueOrDefault(key);
        record.SpotTypes[key] = new CapacityCount(
            checked(current.Active + spotType.Count * activeDelta),
            checked(current.Pending + spotType.Count * pendingDelta));
        RequireNonNegative(record);
    }

    private static void RequireNonNegative(CapacityRecord record)
    {
        if (record.ActorsActive < 0
            || record.ActorsPending < 0
            || record.SpotsActive < 0
            || record.SpotsPending < 0
            || record.SpotTypes.Values.Any(value =>
                value.Active < 0 || value.Pending < 0))
            throw new InvalidDataException(
                "The Location Store capacity record is inconsistent.");
    }

    private static void AddCondition(
        ICollection<ZLinkStoreCondition> conditions,
        ZLinkStoreCondition condition)
    {
        var key = condition switch
        {
            ZLinkStoreCondition.Missing value => value.Key,
            ZLinkStoreCondition.Version value => value.Key,
            _ => throw new ArgumentOutOfRangeException(nameof(condition))
        };
        if (conditions.Any(existing => existing switch
            {
                ZLinkStoreCondition.Missing value => value.Key == key,
                ZLinkStoreCondition.Version value => value.Key == key,
                _ => false
            }))
            return;
        conditions.Add(condition);
    }

    private static void ValidateAuthorityKey(ZLinkAuthorityKey key) =>
        ArgumentException.ThrowIfNullOrWhiteSpace(key.Value);

    private static void ValidateAuthorityPayload(ReadOnlyMemory<byte> payload)
    {
        if (payload.Length > 1024 * 1024)
            throw new ArgumentOutOfRangeException(nameof(payload));
    }

    private static void ValidateAuthorityMutation(ZLinkAuthorityMutation mutation)
    {
        if (mutation is not ZLinkAuthorityMutation.Put put) return;
        var preserve = put.GenerationTransition
                       == ZLinkAuthorityGenerationTransition.Preserve;
        var changesOwner = put.GenerationTransition
                           == ZLinkAuthorityGenerationTransition.NewOwner;
        if (!preserve && !changesOwner
            || preserve && put.TargetOwner is not null
            || changesOwner && (put.TargetOwner is null
                                || put.RelocationCapacityFence is null))
            throw new ArgumentException(
                "The authority mutation is inconsistent.",
                nameof(mutation));
    }

    private static void ValidateReservation(
        ZLinkObjectReservationRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        ValidateAuthorityKey(request.Key);
        ArgumentException.ThrowIfNullOrWhiteSpace(request.StableType);
        ArgumentException.ThrowIfNullOrWhiteSpace(
            request.CreationIntentReference);
        if (request.CreationIntentHash.Length != 32
            || request.CreationIntentEncodedSize is < 0 or > 1024 * 1024
            || request.CreatingPayload.Length > 1024 * 1024
            || request.TargetNodeLifecycleGeneration == 0
            || request.TargetOwner.LeaseGeneration <= 0)
            throw new ArgumentOutOfRangeException(nameof(request));
    }

    private static void ValidateRelocationRequest(
        ZLinkRelocationCapacityReservationRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (request.ReservationId == Guid.Empty
            || string.IsNullOrWhiteSpace(request.Key.Value)
            || string.IsNullOrWhiteSpace(request.ExpectedStoreVersion)
            || string.IsNullOrWhiteSpace(request.StableType)
            || request.SourceNodeLifecycleGeneration == 0
            || request.TargetNodeLifecycleGeneration == 0
            || request.SourceOwner.LeaseGeneration <= 0
            || request.TargetOwner.LeaseGeneration <= 0)
            throw new ArgumentException(
                "The relocation capacity reservation is invalid.",
                nameof(request));
    }

    private static void ValidateAggregate(ZLinkAggregatePrepareRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (request.AggregateId == Guid.Empty
            || request.AggregateGeneration is 0 or > long.MaxValue
            || request.Participants.Count is < 1 or > 1024
            || request.InventoryDigest.Length != 32
            || request.TargetDescriptorLifecycleGeneration == 0
            || request.TargetOwner.LeaseGeneration <= 0
            || request.Participants.Select(value => value.Key.Value)
                .Distinct(StringComparer.Ordinal).Count()
            != request.Participants.Count)
            throw new ArgumentOutOfRangeException(nameof(request));
    }

    private static bool AggregateRequestsEqual(
        ZLinkAggregatePrepareRequest left,
        ZLinkAggregatePrepareRequest right) =>
        Encode(left).AsSpan().SequenceEqual(Encode(right));

    private static void ValidateTerminal(
        ZLinkCreationTerminalPublication publication)
    {
        ValidateCreationOperation(publication.Operation);
        if (publication.TerminalEnvelope.Length > 1024 * 1024
            || publication.TerminalEnvelopeSha256.Length != 32)
            throw new ArgumentException(
                "The creation terminal publication is invalid.",
                nameof(publication));
        Span<byte> hash = stackalloc byte[32];
        SHA256.HashData(publication.TerminalEnvelope.Span, hash);
        if (!CryptographicOperations.FixedTimeEquals(
                hash,
                publication.TerminalEnvelopeSha256.Span))
            throw new ArgumentException(
                "The creation terminal checksum is invalid.",
                nameof(publication));
    }

    private static void ValidateCreationOperation(
        ZLinkCreationOperationId operation)
    {
        if (operation.SourceNodeRid.IsEmpty
            || operation.SourceNodeGeneration == 0
            || operation.OperationIdHigh == 0
            && operation.OperationIdLow == 0)
            throw new ArgumentOutOfRangeException(nameof(operation));
    }

    private static byte[] Sha256(ReadOnlyMemory<byte> payload) =>
        SHA256.HashData(payload.Span);

    private static byte[] Encode<T>(T value) =>
        JsonSerializer.SerializeToUtf8Bytes(
            value,
            ZLinkJsonSerializerOptions.Default);

    private static ZLinkStoreKey AuthorityMetaKey(ZLinkAuthorityKey key) =>
        Key(AuthorityMetaPrefix(key.Value));

    private static ZLinkStoreKey AuthorityPayloadKey(ZLinkAuthorityKey key) =>
        Key($"{AuthorityPrefix}payload:{EncodeSegment(key.Value)}");

    private static ZLinkStoreKey GenerationKey(ZLinkAuthorityKey key) =>
        Key($"{AuthorityPrefix}generation:{EncodeSegment(key.Value)}");

    private static string AuthorityMetaPrefix(string prefix) =>
        $"{AuthorityMetaPrefixValue}{prefix}";

    private static string AuthorityMetaPrefixValue =>
        $"{AuthorityPrefix}meta:";

    private static ZLinkAuthorityKey DecodeAuthorityKey(ZLinkStoreKey key)
    {
        return new ZLinkAuthorityKey(
            key.Value[AuthorityMetaPrefixValue.Length..]);
    }

    private static ZLinkStoreKey ReservationKey(string reservationId) =>
        Key($"{ReservationPrefix}{EncodeSegment(reservationId)}");

    private static ZLinkStoreKey TerminalKey(
        ZLinkCreationOperationId operation) =>
        Key($"{TerminalPrefix}meta:{CreationOperationSegment(operation)}");

    private static ZLinkStoreKey TerminalPayloadKey(
        ZLinkCreationOperationId operation) =>
        Key($"{TerminalPrefix}payload:{CreationOperationSegment(operation)}");

    private static string CreationOperationSegment(
        ZLinkCreationOperationId operation) =>
        $"{operation.SourceNodeRid.ToHex()}:{operation.SourceNodeGeneration}:"
        + $"{operation.OperationIdHigh}:{operation.OperationIdLow}";

    private static ZLinkStoreKey RelocationKey(
        ZLinkRelocationCapacityFence fence) =>
        Key($"{RelocationCapacityPrefix}{EncodeSegment(fence.Value)}");

    private static ZLinkStoreKey AggregateKey(ZLinkAggregateFence fence) =>
        Key($"{AggregatePrefix}{fence.AggregateId:N}:"
            + fence.AggregateGeneration);

    private static ZLinkStoreKey AggregateLockKey(ZLinkAuthorityKey key) =>
        Key($"{AggregateLockPrefix}{EncodeSegment(key.Value)}");

    private static ZLinkStoreKey CapacityKey(
        ZLinkMeshNodeDescriptorKey descriptor,
        ulong lifecycleGeneration) =>
        Key($"{Prefix}capacity:{EncodeSegment(descriptor.MeshName)}"
            + $"{EncodeSegment(descriptor.Rid.ToHex())}"
            + lifecycleGeneration);

    private static string CapacityTypeKey(
        ZLinkPlacementObjectKind kind,
        string stableType) =>
        $"{(int)kind}:{stableType}";

    private sealed record AuthorityMeta(
        byte[] PayloadSha256,
        ulong ObjectGeneration,
        ulong AuthorityOwnerGeneration,
        string OwnerId,
        long OwnerLeaseGeneration,
        ZLinkPlacementAllocation Allocation,
        ZLinkReservedObjectCreation? ReservedCreation);

    private sealed record StoredAuthority(
        ZLinkAuthorityKey Key,
        AuthorityMeta Meta,
        ZLinkStoreVersion Version,
        ZLinkAuthoritySnapshot Snapshot);

    private sealed record ReservationRecord(
        ZLinkAuthorityKey Key,
        ulong ObjectGeneration,
        ulong AuthorityOwnerGeneration,
        string ReservationId,
        ZLinkMeshNodeDescriptorKey TargetDescriptor,
        ulong TargetLifecycleGeneration,
        ZLinkLocationOwnerToken TargetOwner,
        ReservationStatus Status);

    private sealed record TerminalMeta(
        ZLinkCreationTerminalRecord Record,
        byte[] PayloadSha256);

    private sealed record StoredTerminal(
        ZLinkCreationTerminalRecord Record,
        ZLinkStoreVersion Version);

    private sealed record RelocationRecordState(
        ZLinkRelocationCapacityReservationRequest Request,
        RelocationStatus Status);

    private sealed record AggregateRecord(
        ZLinkAggregatePrepareRequest Request,
        AggregateStatus Status);

    private sealed record StoredRecord<T>(
        T Record,
        ZLinkStoreVersion Version,
        DateTimeOffset StoreNow);

    private sealed record StoredOwner(
        ZLinkLocationOwnerToken Token,
        ZLinkStoreVersion Version);

    private sealed record StoredTarget(
        ZLinkMeshNodeDescriptor Descriptor,
        ZLinkStoreCondition DescriptorCondition,
        ZLinkStoreCondition OwnerCondition);

    private sealed record StoredCapacity(
        ZLinkStoreKey Key,
        CapacityRecord Record,
        ZLinkStoreCondition Condition);

    private sealed record GenerationRecord(
        ulong ObjectGeneration,
        ulong AuthorityOwnerGeneration);

    private sealed record GenerationState(
        ulong ObjectGeneration,
        ulong AuthorityOwnerGeneration,
        ZLinkStoreCondition Condition);

    private sealed class CapacityRecord
    {
        public int ActorsActive { get; set; }
        public int ActorsPending { get; set; }
        public int SpotsActive { get; set; }
        public int SpotsPending { get; set; }
        public Dictionary<string, CapacityCount> SpotTypes { get; init; } =
            new(StringComparer.Ordinal);

        public CapacityRecord Clone() =>
            new()
            {
                ActorsActive = ActorsActive,
                ActorsPending = ActorsPending,
                SpotsActive = SpotsActive,
                SpotsPending = SpotsPending,
                SpotTypes = SpotTypes.ToDictionary(
                    static pair => pair.Key,
                    static pair => pair.Value,
                    StringComparer.Ordinal)
            };
    }

    private readonly record struct CapacityCount(int Active, int Pending);

    private enum ReservationStatus
    {
        Reserved = 1,
        Created = 2,
        Rejected = 3,
        Failed = 4,
        Aborted = 5
    }

    private enum RelocationStatus
    {
        Reserved = 1,
        Prepared = 2,
        Committed = 3,
        Aborted = 4
    }

    private enum AggregateStatus
    {
        Prepared = 1,
        Committed = 2,
        Aborted = 3
    }
}
