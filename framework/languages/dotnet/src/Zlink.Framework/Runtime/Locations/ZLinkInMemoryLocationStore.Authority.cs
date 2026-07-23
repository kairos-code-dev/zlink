using System.Text;

namespace Zlink.Framework.Runtime.Locations;

internal sealed partial class ZLinkInMemoryLocationStore
{
    private readonly Dictionary<string, ZLinkAuthoritySnapshot> _authorities =
        new(StringComparer.Ordinal);
    private readonly Dictionary<string, AuthorityScan> _authorityScans =
        new(StringComparer.Ordinal);
    private readonly Dictionary<string, ReservationState> _authorityReservations =
        new(StringComparer.Ordinal);
    private readonly Dictionary<ZLinkAggregateFence, AggregateState> _authorityAggregates = [];
    private long _authorityRevision;
    private long _authorityObjectGeneration;
    private long _authorityOwnerGeneration;

    public ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken = default)
    {
        ValidateAuthorityKey(key);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            return ValueTask.FromResult<ZLinkAuthorityReadResult>(
                _authorities.TryGetValue(key.Value, out var snapshot)
                    ? new ZLinkAuthorityReadResult.Found(
                        snapshot with { StoreNow = _time.GetUtcNow() })
                    : new ZLinkAuthorityReadResult.Missing(_time.GetUtcNow()));
        }
    }

    public ValueTask<ZLinkAuthorityCompareExchangeResult>
        CompareExchangeAuthorityAsync(
            ZLinkAuthorityKey key,
            ZLinkAuthorityExpectation expectation,
            ZLinkAuthorityMutation mutation,
            CancellationToken cancellationToken = default)
    {
        ValidateAuthorityKey(key);
        ArgumentNullException.ThrowIfNull(expectation);
        ArgumentNullException.ThrowIfNull(mutation);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            _authorities.TryGetValue(key.Value, out var current);
            if (!Matches(expectation, current))
            {
                return ValueTask.FromResult<ZLinkAuthorityCompareExchangeResult>(
                    new ZLinkAuthorityCompareExchangeResult.Conflict(
                        current is null
                            ? new ZLinkAuthorityReadResult.Missing(now)
                            : new ZLinkAuthorityReadResult.Found(
                                current with { StoreNow = now })));
            }

            if (mutation is ZLinkAuthorityMutation.Delete)
            {
                if (!CanIncrement(_authorityRevision))
                    return ValueTask.FromResult<ZLinkAuthorityCompareExchangeResult>(
                        new ZLinkAuthorityCompareExchangeResult.GenerationExhausted());
                var version = Next(ref _authorityRevision).ToString();
                _authorities.Remove(key.Value);
                return ValueTask.FromResult<ZLinkAuthorityCompareExchangeResult>(
                    new ZLinkAuthorityCompareExchangeResult.Deleted(version, now));
            }

            var put = (ZLinkAuthorityMutation.Put)mutation;
            ValidateAuthorityPayload(put.Payload);
            var needsObject = put.GenerationTransition
                              == ZLinkAuthorityGenerationTransition.NewObject;
            var needsOwner = put.GenerationTransition is
                ZLinkAuthorityGenerationTransition.NewObject
                or ZLinkAuthorityGenerationTransition.NewOwner;
            if (!CanIncrement(_authorityRevision)
                || needsObject && !CanIncrement(_authorityObjectGeneration)
                || needsOwner && !CanIncrement(_authorityOwnerGeneration))
                return ValueTask.FromResult<ZLinkAuthorityCompareExchangeResult>(
                    new ZLinkAuthorityCompareExchangeResult.GenerationExhausted());

            var owner = ResolveMutationOwner(put, current);
            var stored = new ZLinkAuthoritySnapshot(
                Next(ref _authorityRevision).ToString(),
                put.Payload.ToArray(),
                needsObject
                    ? checked((ulong)Next(ref _authorityObjectGeneration))
                    : current!.ObjectGeneration,
                needsOwner
                    ? checked((ulong)Next(ref _authorityOwnerGeneration))
                    : current!.AuthorityOwnerGeneration,
                owner.OwnerId,
                checked((long)owner.Generation),
                now);
            _authorities[key.Value] = stored;
            return ValueTask.FromResult<ZLinkAuthorityCompareExchangeResult>(
                new ZLinkAuthorityCompareExchangeResult.Stored(stored));
        }
    }

    public ValueTask<ZLinkAuthorityScanResult> ListAuthoritiesAsync(
        string prefix,
        ZLinkAuthorityScanCursor? cursor,
        int limit,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(prefix);
        if (limit is < 1 or > 1000)
            throw new ArgumentOutOfRangeException(nameof(limit));
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            AuthorityScan scan;
            var position = 0;
            if (cursor is null)
            {
                var id = Guid.NewGuid().ToString("N");
                scan = new AuthorityScan(
                    _authorities
                        .Where(pair => pair.Key.StartsWith(
                            prefix,
                            StringComparison.Ordinal))
                        .OrderBy(static pair => pair.Key, StringComparer.Ordinal)
                        .Select(pair => new ZLinkAuthorityEntry(
                            new ZLinkAuthorityKey(pair.Key),
                            pair.Value))
                        .ToArray(),
                    _time.GetUtcNow() + TimeSpan.FromMinutes(1));
                _authorityScans[id] = scan;
                cursor = new ZLinkAuthorityScanCursor($"{id}:0");
            }
            else
            {
                var separator = cursor.Value.Encoded.LastIndexOf(':');
                if (separator <= 0
                    || !int.TryParse(
                        cursor.Value.Encoded[(separator + 1)..],
                        out position)
                    || !_authorityScans.TryGetValue(
                        cursor.Value.Encoded[..separator],
                        out scan!)
                    || scan.ExpiresAt <= _time.GetUtcNow())
                {
                    return ValueTask.FromResult<ZLinkAuthorityScanResult>(
                        new ZLinkAuthorityScanResult.ScanExpired());
                }
            }

            var scanId = cursor.Value.Encoded[
                ..cursor.Value.Encoded.LastIndexOf(':')];
            var items = scan.Items.Skip(position).Take(limit).ToArray();
            var nextPosition = position + items.Length;
            ZLinkAuthorityScanCursor? next = nextPosition < scan.Items.Count
                ? new ZLinkAuthorityScanCursor($"{scanId}:{nextPosition}")
                : null;
            if (next is null)
                _authorityScans.Remove(scanId);
            return ValueTask.FromResult<ZLinkAuthorityScanResult>(
                new ZLinkAuthorityScanResult.Page(
                    new ZLinkAuthorityPage(items, next)));
        }
    }

    public ValueTask<ZLinkObjectReserveResult> ReserveAsync(
        ZLinkObjectReservationRequest request,
        CancellationToken cancellationToken = default)
    {
        ValidateReservationRequest(request);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var now = _time.GetUtcNow();
            if (_authorities.TryGetValue(request.Key.Value, out var existing))
                return ValueTask.FromResult<ZLinkObjectReserveResult>(
                    new ZLinkObjectReserveResult.AlreadyExists(existing));
            if (!CanIncrement(_authorityRevision)
                || !CanIncrement(_authorityObjectGeneration)
                || !CanIncrement(_authorityOwnerGeneration))
                return ValueTask.FromResult<ZLinkObjectReserveResult>(
                    new ZLinkObjectReserveResult.GenerationExhausted());

            var reservationVersion = Guid.NewGuid().ToString("N");
            var snapshot = new ZLinkAuthoritySnapshot(
                Next(ref _authorityRevision).ToString(),
                Encoding.UTF8.GetBytes(request.CreationIntentReference),
                checked((ulong)Next(ref _authorityObjectGeneration)),
                checked((ulong)Next(ref _authorityOwnerGeneration)),
                request.TargetOwner.OwnerId,
                checked((long)request.TargetOwner.Generation),
                now);
            _authorities[request.Key.Value] = snapshot;
            var reservation = new ZLinkObjectReservation(
                request.Key,
                snapshot.StoreVersion,
                snapshot.ObjectGeneration,
                snapshot.AuthorityOwnerGeneration,
                reservationVersion,
                request.TargetDescriptor,
                request.TargetOwner);
            _authorityReservations[reservationVersion] =
                new ReservationState(reservation, ReservationStatus.Reserved);
            return ValueTask.FromResult<ZLinkObjectReserveResult>(
                new ZLinkObjectReserveResult.Reserved(reservation));
        }
    }

    public ValueTask<ZLinkObjectCommitResult> CommitAsync(
        ZLinkObjectReservation reservation,
        ReadOnlyMemory<byte> readyPayload,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(reservation);
        ValidateAuthorityPayload(readyPayload);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!_authorityReservations.TryGetValue(
                    reservation.ReservationVersion,
                    out var state)
                || state.Reservation != reservation)
                return ValueTask.FromResult<ZLinkObjectCommitResult>(
                    new ZLinkObjectCommitResult.Stale());
            if (state.Status == ReservationStatus.Committed)
                return ValueTask.FromResult<ZLinkObjectCommitResult>(
                    new ZLinkObjectCommitResult.AlreadyCommitted(state.Snapshot!));
            if (state.Status == ReservationStatus.Aborted
                || !_authorities.TryGetValue(
                    reservation.Key.Value,
                    out var current)
                || current.StoreVersion != reservation.StoreVersion)
                return ValueTask.FromResult<ZLinkObjectCommitResult>(
                    new ZLinkObjectCommitResult.Stale());
            if (!CanIncrement(_authorityRevision))
                return ValueTask.FromResult<ZLinkObjectCommitResult>(
                    new ZLinkObjectCommitResult.GenerationExhausted());

            var stored = current with
            {
                StoreVersion = Next(ref _authorityRevision).ToString(),
                Payload = readyPayload.ToArray(),
                StoreNow = _time.GetUtcNow()
            };
            _authorities[reservation.Key.Value] = stored;
            state.Status = ReservationStatus.Committed;
            state.Snapshot = stored;
            return ValueTask.FromResult<ZLinkObjectCommitResult>(
                new ZLinkObjectCommitResult.Committed(stored));
        }
    }

    public ValueTask<ZLinkObjectAbortResult> AbortAsync(
        ZLinkObjectReservation reservation,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(reservation);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!_authorityReservations.TryGetValue(
                    reservation.ReservationVersion,
                    out var state)
                || state.Reservation != reservation)
                return ValueTask.FromResult<ZLinkObjectAbortResult>(
                    new ZLinkObjectAbortResult.Stale());
            if (state.Status == ReservationStatus.Aborted)
                return ValueTask.FromResult<ZLinkObjectAbortResult>(
                    new ZLinkObjectAbortResult.AlreadyAborted());
            if (state.Status == ReservationStatus.Committed
                || !_authorities.TryGetValue(
                    reservation.Key.Value,
                    out var current)
                || current.StoreVersion != reservation.StoreVersion)
                return ValueTask.FromResult<ZLinkObjectAbortResult>(
                    new ZLinkObjectAbortResult.Stale());
            if (!CanIncrement(_authorityRevision))
                return ValueTask.FromResult<ZLinkObjectAbortResult>(
                    new ZLinkObjectAbortResult.GenerationExhausted());

            Next(ref _authorityRevision);
            _authorities.Remove(reservation.Key.Value);
            state.Status = ReservationStatus.Aborted;
            return ValueTask.FromResult<ZLinkObjectAbortResult>(
                new ZLinkObjectAbortResult.Aborted());
        }
    }

    public ValueTask<ZLinkAggregatePrepareResult> PrepareAggregateAsync(
        ZLinkAggregatePrepareRequest request,
        CancellationToken cancellationToken = default)
    {
        ValidateAggregateRequest(request);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            var fence = new ZLinkAggregateFence(
                request.AggregateId,
                request.AggregateGeneration);
            if (_authorityAggregates.TryGetValue(fence, out var existing))
            {
                return ValueTask.FromResult<ZLinkAggregatePrepareResult>(
                    existing.Status == AggregateStatus.Prepared
                        ? new ZLinkAggregatePrepareResult.AlreadyPrepared(fence)
                        : new ZLinkAggregatePrepareResult.Stale());
            }
            if (request.Participants.Any(participant =>
                    !_authorities.TryGetValue(
                        participant.Key.Value,
                        out var current)
                    || current.StoreVersion != participant.ExpectedStoreVersion))
                return ValueTask.FromResult<ZLinkAggregatePrepareResult>(
                    new ZLinkAggregatePrepareResult.Conflict());

            _authorityAggregates[fence] =
                new AggregateState(request, AggregateStatus.Prepared);
            return ValueTask.FromResult<ZLinkAggregatePrepareResult>(
                new ZLinkAggregatePrepareResult.Prepared(fence));
        }
    }

    public ValueTask<ZLinkAggregateCommitResult> CommitAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!_authorityAggregates.TryGetValue(fence, out var aggregate)
                || aggregate.Status == AggregateStatus.Aborted)
                return ValueTask.FromResult(ZLinkAggregateCommitResult.Stale);
            if (aggregate.Status == AggregateStatus.Committed)
                return ValueTask.FromResult(
                    ZLinkAggregateCommitResult.AlreadyCommitted);

            var ownerTransitions = aggregate.Request.Participants.Count(
                static participant =>
                    participant.OwnerTransition is
                        ZLinkAuthorityGenerationTransition.NewOwner
                        or ZLinkAuthorityGenerationTransition.NewObject);
            if (_authorityRevision > long.MaxValue
                    - aggregate.Request.Participants.Count
                || _authorityOwnerGeneration > long.MaxValue - ownerTransitions)
                return ValueTask.FromResult(
                    ZLinkAggregateCommitResult.GenerationExhausted);
            if (aggregate.Request.Participants.Any(participant =>
                    !_authorities.TryGetValue(
                        participant.Key.Value,
                        out var current)
                    || current.StoreVersion != participant.ExpectedStoreVersion))
                return ValueTask.FromResult(ZLinkAggregateCommitResult.Stale);

            var now = _time.GetUtcNow();
            foreach (var participant in aggregate.Request.Participants)
            {
                var current = _authorities[participant.Key.Value];
                var changesOwner = participant.OwnerTransition is
                    ZLinkAuthorityGenerationTransition.NewOwner
                    or ZLinkAuthorityGenerationTransition.NewObject;
                var stored = current with
                {
                    StoreVersion = Next(ref _authorityRevision).ToString(),
                    Payload = participant.AuthorityPayload.ToArray(),
                    AuthorityOwnerGeneration = changesOwner
                        ? checked((ulong)Next(ref _authorityOwnerGeneration))
                        : current.AuthorityOwnerGeneration,
                    OwnerId = changesOwner
                        ? aggregate.Request.TargetOwner.OwnerId
                        : current.OwnerId,
                    OwnerLeaseGeneration = changesOwner
                        ? checked((long)aggregate.Request.TargetOwner.Generation)
                        : current.OwnerLeaseGeneration,
                    StoreNow = now
                };
                _authorities[participant.Key.Value] = stored;
            }
            aggregate.Status = AggregateStatus.Committed;
            return ValueTask.FromResult(ZLinkAggregateCommitResult.Committed);
        }
    }

    public ValueTask<ZLinkAggregateAbortResult> AbortAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!_authorityAggregates.TryGetValue(fence, out var aggregate))
                return ValueTask.FromResult(ZLinkAggregateAbortResult.Stale);
            if (aggregate.Status == AggregateStatus.Aborted)
                return ValueTask.FromResult(
                    ZLinkAggregateAbortResult.AlreadyAborted);
            if (aggregate.Status == AggregateStatus.Committed)
                return ValueTask.FromResult(ZLinkAggregateAbortResult.Stale);
            aggregate.Status = AggregateStatus.Aborted;
            return ValueTask.FromResult(ZLinkAggregateAbortResult.Aborted);
        }
    }

    private static bool Matches(
        ZLinkAuthorityExpectation expectation,
        ZLinkAuthoritySnapshot? current) =>
        expectation switch
        {
            ZLinkAuthorityExpectation.Missing => current is null,
            ZLinkAuthorityExpectation.Found found =>
                current is not null
                && string.Equals(
                    current.StoreVersion,
                    found.StoreVersion,
                    StringComparison.Ordinal),
            _ => false
        };

    private static ZLinkLocationOwnerToken ResolveMutationOwner(
        ZLinkAuthorityMutation.Put put,
        ZLinkAuthoritySnapshot? current)
    {
        if (put.GenerationTransition == ZLinkAuthorityGenerationTransition.Preserve)
            return new ZLinkLocationOwnerToken(
                current!.OwnerId,
                checked((ulong)current.OwnerLeaseGeneration));
        if (ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                put.Payload.Span,
                out var relocation))
            return new ZLinkLocationOwnerToken(
                relocation.TargetOwnerId,
                checked((ulong)relocation.TargetOwnerLeaseGeneration));
        throw new InvalidOperationException(
            "The authority mutation contract does not carry the target owner fence.");
    }

    private static void ValidateAuthorityKey(ZLinkAuthorityKey key) =>
        ArgumentException.ThrowIfNullOrWhiteSpace(key.Value);

    private static void ValidateAuthorityPayload(ReadOnlyMemory<byte> payload)
    {
        if (payload.Length > 1024 * 1024)
            throw new ArgumentOutOfRangeException(nameof(payload));
    }

    private static void ValidateReservationRequest(
        ZLinkObjectReservationRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        ValidateAuthorityKey(request.Key);
        ArgumentException.ThrowIfNullOrWhiteSpace(request.StableType);
        ArgumentException.ThrowIfNullOrWhiteSpace(
            request.CreationIntentReference);
        if (request.CreationIntentHash.Length != 32
            || request.CreationIntentEncodedSize is < 0 or > 1024 * 1024
            || request.PendingCapacityDelta <= 0
            || request.TargetOwner.Generation is 0 or > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(request));
    }

    private static void ValidateAggregateRequest(
        ZLinkAggregatePrepareRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (request.AggregateId == Guid.Empty
            || request.AggregateGeneration is 0 or > long.MaxValue
            || request.Participants.Count is < 1 or > 1024
            || request.InventoryDigest.Length != 32
            || request.TargetOwner.Generation is 0 or > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(request));
        if (request.Participants.Select(static value => value.Key.Value)
            .Distinct(StringComparer.Ordinal).Count() != request.Participants.Count)
            throw new ArgumentException(
                "Aggregate participant keys must be unique.",
                nameof(request));
    }

    private static bool CanIncrement(long value) => value < long.MaxValue;

    private static long Next(ref long value) => checked(++value);

    private sealed record AuthorityScan(
        IReadOnlyList<ZLinkAuthorityEntry> Items,
        DateTimeOffset ExpiresAt);

    private sealed class ReservationState(
        ZLinkObjectReservation reservation,
        ReservationStatus status)
    {
        internal ZLinkObjectReservation Reservation { get; } = reservation;
        internal ReservationStatus Status { get; set; } = status;
        internal ZLinkAuthoritySnapshot? Snapshot { get; set; }
    }

    private sealed class AggregateState(
        ZLinkAggregatePrepareRequest request,
        AggregateStatus status)
    {
        internal ZLinkAggregatePrepareRequest Request { get; } = request;
        internal AggregateStatus Status { get; set; } = status;
    }

    private enum ReservationStatus
    {
        Reserved,
        Committed,
        Aborted
    }

    private enum AggregateStatus
    {
        Prepared,
        Committed,
        Aborted
    }
}
