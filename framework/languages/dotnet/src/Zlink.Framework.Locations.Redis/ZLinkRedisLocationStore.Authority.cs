using System.Text;
using StackExchange.Redis;

namespace Zlink.Framework.Locations.Redis;

public sealed partial class ZLinkRedisLocationStore
{
    private static readonly TimeSpan AuthorityScanRetention =
        TimeSpan.FromMinutes(1);

    public async ValueTask<ZLinkAuthorityReadResult> ReadAuthorityAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken = default)
    {
        ValidateAuthorityKey(key);
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.Read,
                    [
                        _keys.AuthorityVersionsKey(),
                        _keys.AuthorityPayloadsKey(),
                        _keys.AuthorityObjectGenerationsKey(),
                        _keys.AuthorityOwnerGenerationsKey(),
                        _keys.AuthorityOwnerIdsKey(),
                        _keys.AuthorityOwnerLeaseGenerationsKey()
                    ],
                    [key.Value]).ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        var now = DateTimeOffset.FromUnixTimeMilliseconds((long)result[0]);
        return (long)result[1] == 0
            ? new ZLinkAuthorityReadResult.Missing(now)
            : new ZLinkAuthorityReadResult.Found(
                Snapshot(result, 2, now));
    }

    public async ValueTask<ZLinkAuthorityCompareExchangeResult>
        CompareExchangeAuthorityAsync(
            ZLinkAuthorityKey key,
            ZLinkAuthorityExpectation expectation,
            ZLinkAuthorityMutation mutation,
            CancellationToken cancellationToken = default)
    {
        ValidateAuthorityKey(key);
        ArgumentNullException.ThrowIfNull(expectation);
        ArgumentNullException.ThrowIfNull(mutation);
        if (mutation is ZLinkAuthorityMutation.Put put)
        {
            ValidateAuthorityPayload(put.Payload);
            if (put.GenerationTransition
                != ZLinkAuthorityGenerationTransition.Preserve)
            {
                throw new InvalidOperationException(
                    "The public authority mutation does not carry the target owner fence required by NewOwner or NewObject.");
            }
        }

        var expectationName = expectation is ZLinkAuthorityExpectation.Missing
            ? "missing"
            : "found";
        var expectedVersion = expectation is ZLinkAuthorityExpectation.Found found
            ? found.StoreVersion
            : string.Empty;
        var mutationName = mutation is ZLinkAuthorityMutation.Delete
            ? "delete"
            : "put";
        var payload = mutation is ZLinkAuthorityMutation.Put value
            ? value.Payload.ToArray()
            : [];
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.CompareExchange,
                    AuthorityKeys(),
                    [
                        key.Value,
                        expectationName,
                        expectedVersion,
                        mutationName,
                        payload
                    ]).ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        var status = (string)result[0]!;
        var now = DateTimeOffset.FromUnixTimeMilliseconds((long)result[1]);
        return status switch
        {
            "stored" => new ZLinkAuthorityCompareExchangeResult.Stored(
                Snapshot(result, 2, now)),
            "deleted" => new ZLinkAuthorityCompareExchangeResult.Deleted(
                (string)result[2]!,
                now),
            "conflict-missing" => new ZLinkAuthorityCompareExchangeResult.Conflict(
                new ZLinkAuthorityReadResult.Missing(now)),
            "conflict-found" => new ZLinkAuthorityCompareExchangeResult.Conflict(
                new ZLinkAuthorityReadResult.Found(
                    Snapshot(result, 2, now))),
            "exhausted" =>
                new ZLinkAuthorityCompareExchangeResult.GenerationExhausted(),
            _ => throw new InvalidOperationException(
                $"Unknown Redis authority compare-exchange result '{status}'.")
        };
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

        string scanId;
        var position = 0;
        RedisResult[] result;
        if (cursor is null)
        {
            scanId = Guid.NewGuid().ToString("N");
            result = await ExecuteAsync(
                    async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                        ZLinkRedisAuthorityScripts.StartScan,
                        [
                            _keys.AuthorityVersionsKey(),
                            _keys.AuthorityPayloadsKey(),
                            _keys.AuthorityObjectGenerationsKey(),
                            _keys.AuthorityOwnerGenerationsKey(),
                            _keys.AuthorityOwnerIdsKey(),
                            _keys.AuthorityOwnerLeaseGenerationsKey(),
                            _keys.AuthorityMembershipsKey(),
                            _keys.AuthorityIndexKey(),
                            _keys.AuthorityScanKey(scanId)
                        ],
                        [
                            prefix,
                            (long)AuthorityScanRetention.TotalMilliseconds,
                            limit
                        ]).ConfigureAwait(false))!,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        else
        {
            (scanId, position) = ParseCursor(cursor.Value);
            result = await ExecuteAsync(
                    async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                        ZLinkRedisAuthorityScripts.ContinueScan,
                        [_keys.AuthorityScanKey(scanId)],
                        [
                            position,
                            limit,
                            (long)AuthorityScanRetention.TotalMilliseconds
                        ]).ConfigureAwait(false))!,
                    cancellationToken)
                .ConfigureAwait(false);
            if ((string)result[0]! == "expired")
                return new ZLinkAuthorityScanResult.ScanExpired();
            result = [(RedisResult)result[1], result[2], result[3]];
        }

        var now = DateTimeOffset.FromUnixTimeMilliseconds((long)result[0]);
        var total = checked((int)(long)result[1]);
        var fields = (RedisResult[])result[2]!;
        var entries = new List<ZLinkAuthorityEntry>(fields.Length / 7);
        for (var index = 0; index + 6 < fields.Length; index += 7)
        {
            entries.Add(new ZLinkAuthorityEntry(
                new ZLinkAuthorityKey((string)fields[index]!),
                Snapshot(fields, index + 1, now)));
        }

        var nextPosition = position + entries.Count;
        ZLinkAuthorityScanCursor? next = nextPosition < total
            ? new ZLinkAuthorityScanCursor($"{scanId}:{nextPosition}")
            : null;
        return new ZLinkAuthorityScanResult.Page(
            new ZLinkAuthorityPage(entries, next));
    }

    public async ValueTask<ZLinkObjectReserveResult> ReserveAsync(
        ZLinkObjectReservationRequest request,
        CancellationToken cancellationToken = default)
    {
        ValidateReservationRequest(request);
        var reservationVersion = Guid.NewGuid().ToString("N");
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.Reserve,
                    [
                        _keys.AuthorityCountersKey(),
                        _keys.AuthorityVersionsKey(),
                        _keys.AuthorityPayloadsKey(),
                        _keys.AuthorityObjectGenerationsKey(),
                        _keys.AuthorityOwnerGenerationsKey(),
                        _keys.AuthorityOwnerIdsKey(),
                        _keys.AuthorityOwnerLeaseGenerationsKey(),
                        _keys.AuthorityIndexKey(),
                        _keys.LeaseKey(request.TargetOwner.OwnerId),
                        _keys.AuthorityReservationKey(reservationVersion)
                    ],
                    [
                        request.Key.Value,
                        Encoding.UTF8.GetBytes(request.CreationIntentReference),
                        request.TargetOwner.OwnerId,
                        request.TargetOwner.Generation,
                        request.TargetDescriptor.MeshName,
                        request.TargetDescriptor.Rid.ToHex()
                    ]).ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        var status = (string)result[0]!;
        var now = DateTimeOffset.FromUnixTimeMilliseconds((long)result[1]);
        if (status == "exists")
        {
            return new ZLinkObjectReserveResult.AlreadyExists(
                Snapshot(result, 2, now));
        }
        if (status == "owner-stale")
        {
            return new ZLinkObjectReserveResult.Conflict(
                new ZLinkAuthorityReadResult.Missing(now));
        }
        if (status == "exhausted")
            return new ZLinkObjectReserveResult.GenerationExhausted();
        if (status != "reserved")
            throw new InvalidOperationException(
                $"Unknown Redis authority reserve result '{status}'.");

        return new ZLinkObjectReserveResult.Reserved(
            new ZLinkObjectReservation(
                request.Key,
                (string)result[2]!,
                ParseGeneration(result[3]),
                ParseGeneration(result[4]),
                reservationVersion,
                request.TargetDescriptor,
                request.TargetOwner));
    }

    public async ValueTask<ZLinkObjectCommitResult> CommitAsync(
        ZLinkObjectReservation reservation,
        ReadOnlyMemory<byte> readyPayload,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(reservation);
        ValidateAuthorityPayload(readyPayload);
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.CommitReservation,
                    ReservationKeys(reservation.ReservationVersion),
                    [
                        reservation.Key.Value,
                        reservation.StoreVersion,
                        readyPayload.ToArray()
                    ]).ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        var status = (string)result[0]!;
        var now = DateTimeOffset.FromUnixTimeMilliseconds((long)result[1]);
        return status switch
        {
            "committed" => new ZLinkObjectCommitResult.Committed(
                Snapshot(result, 2, now)),
            "already" => new ZLinkObjectCommitResult.AlreadyCommitted(
                Snapshot(result, 2, now)),
            "stale" => new ZLinkObjectCommitResult.Stale(),
            "exhausted" => new ZLinkObjectCommitResult.GenerationExhausted(),
            _ => throw new InvalidOperationException(
                $"Unknown Redis authority commit result '{status}'.")
        };
    }

    public async ValueTask<ZLinkObjectAbortResult> AbortAsync(
        ZLinkObjectReservation reservation,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(reservation);
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.AbortReservation,
                    ReservationKeys(reservation.ReservationVersion),
                    [reservation.Key.Value, reservation.StoreVersion])
                    .ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        return (string)result[0]! switch
        {
            "aborted" => new ZLinkObjectAbortResult.Aborted(),
            "already" => new ZLinkObjectAbortResult.AlreadyAborted(),
            "stale" => new ZLinkObjectAbortResult.Stale(),
            "exhausted" => new ZLinkObjectAbortResult.GenerationExhausted(),
            var status => throw new InvalidOperationException(
                $"Unknown Redis authority abort result '{status}'.")
        };
    }

    public async ValueTask<ZLinkAggregatePrepareResult> PrepareAggregateAsync(
        ZLinkAggregatePrepareRequest request,
        CancellationToken cancellationToken = default)
    {
        ValidateAggregateRequest(request);
        var fence = new ZLinkAggregateFence(
            request.AggregateId,
            request.AggregateGeneration);
        var arguments = new List<RedisValue>(
            3 + request.Participants.Count * 5)
        {
            request.TargetOwner.OwnerId,
            request.TargetOwner.Generation,
            request.Participants.Count
        };
        foreach (var participant in request.Participants)
        {
            arguments.Add(participant.Key.Value);
            arguments.Add(participant.ExpectedStoreVersion);
            arguments.Add((int)participant.OwnerTransition);
            arguments.Add(participant.AuthorityPayload.ToArray());
            arguments.Add(participant.MembershipMutation.ToArray());
        }

        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.PrepareAggregate,
                    AggregateKeys(fence, request.TargetOwner.OwnerId),
                    [.. arguments]).ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        return (string)result[0]! switch
        {
            "prepared" => new ZLinkAggregatePrepareResult.Prepared(fence),
            "already" => new ZLinkAggregatePrepareResult.AlreadyPrepared(fence),
            "conflict" => new ZLinkAggregatePrepareResult.Conflict(),
            "stale" => new ZLinkAggregatePrepareResult.Stale(),
            "exhausted" =>
                new ZLinkAggregatePrepareResult.GenerationExhausted(),
            var status => throw new InvalidOperationException(
                $"Unknown Redis aggregate prepare result '{status}'.")
        };
    }

    public async ValueTask<ZLinkAggregateCommitResult> CommitAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default)
    {
        ValidateFence(fence);
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.CommitAggregate,
                    AggregateKeys(fence, ownerId: string.Empty),
                    []).ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        return (string)result[0]! switch
        {
            "committed" => ZLinkAggregateCommitResult.Committed,
            "already" => ZLinkAggregateCommitResult.AlreadyCommitted,
            "stale" => ZLinkAggregateCommitResult.Stale,
            "exhausted" => ZLinkAggregateCommitResult.GenerationExhausted,
            var status => throw new InvalidOperationException(
                $"Unknown Redis aggregate commit result '{status}'.")
        };
    }

    public async ValueTask<ZLinkAggregateAbortResult> AbortAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default)
    {
        ValidateFence(fence);
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.AbortAggregate,
                    [_keys.AuthorityAggregateKey(fence)],
                    []).ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        return (string)result[0]! switch
        {
            "aborted" => ZLinkAggregateAbortResult.Aborted,
            "already" => ZLinkAggregateAbortResult.AlreadyAborted,
            "stale" => ZLinkAggregateAbortResult.Stale,
            var status => throw new InvalidOperationException(
                $"Unknown Redis aggregate abort result '{status}'.")
        };
    }

    private RedisKey[] AuthorityKeys() =>
    [
        _keys.AuthorityCountersKey(),
        _keys.AuthorityVersionsKey(),
        _keys.AuthorityPayloadsKey(),
        _keys.AuthorityObjectGenerationsKey(),
        _keys.AuthorityOwnerGenerationsKey(),
        _keys.AuthorityOwnerIdsKey(),
        _keys.AuthorityOwnerLeaseGenerationsKey(),
        _keys.AuthorityMembershipsKey(),
        _keys.AuthorityIndexKey()
    ];

    private RedisKey[] ReservationKeys(string reservationVersion) =>
    [
        _keys.AuthorityCountersKey(),
        _keys.AuthorityVersionsKey(),
        _keys.AuthorityPayloadsKey(),
        _keys.AuthorityObjectGenerationsKey(),
        _keys.AuthorityOwnerGenerationsKey(),
        _keys.AuthorityOwnerIdsKey(),
        _keys.AuthorityOwnerLeaseGenerationsKey(),
        _keys.AuthorityIndexKey(),
        _keys.AuthorityReservationKey(reservationVersion)
    ];

    private RedisKey[] AggregateKeys(
        ZLinkAggregateFence fence,
        string ownerId) =>
    [
        _keys.AuthorityVersionsKey(),
        _keys.AuthorityPayloadsKey(),
        _keys.AuthorityObjectGenerationsKey(),
        _keys.AuthorityOwnerGenerationsKey(),
        _keys.AuthorityOwnerIdsKey(),
        _keys.AuthorityOwnerLeaseGenerationsKey(),
        _keys.AuthorityMembershipsKey(),
        _keys.AuthorityCountersKey(),
        _keys.AuthorityAggregateKey(fence),
        string.IsNullOrEmpty(ownerId)
            ? _keys.LeaseKey("__unused__")
            : _keys.LeaseKey(ownerId)
    ];

    private static ZLinkAuthoritySnapshot Snapshot(
        RedisResult[] values,
        int offset,
        DateTimeOffset storeNow) =>
        new(
            (string)values[offset]!,
            ToBytes(values[offset + 1]),
            ParseGeneration(values[offset + 2]),
            ParseGeneration(values[offset + 3]),
            (string)values[offset + 4]!,
            checked((long)ParseGeneration(values[offset + 5])),
            storeNow);

    private static byte[] ToBytes(RedisResult result) =>
        (byte[]?)(RedisValue)result!
        ?? throw new InvalidDataException(
            "Redis authority payload was unexpectedly null.");

    private static ulong ParseGeneration(RedisResult result) =>
        ulong.Parse((string)result!, System.Globalization.CultureInfo.InvariantCulture);

    private static (string ScanId, int Position) ParseCursor(
        ZLinkAuthorityScanCursor cursor)
    {
        var separator = cursor.Encoded.LastIndexOf(':');
        if (separator <= 0
            || !int.TryParse(
                cursor.Encoded[(separator + 1)..],
                out var position)
            || position < 0)
            throw new ArgumentException(
                "The Redis authority scan cursor is invalid.",
                nameof(cursor));
        return (cursor.Encoded[..separator], position);
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
        if (request.Participants.Any(static value =>
                value.AuthorityPayload.Length > 1024 * 1024))
            throw new ArgumentOutOfRangeException(nameof(request));
    }

    private static void ValidateFence(ZLinkAggregateFence fence)
    {
        if (fence.AggregateId == Guid.Empty
            || fence.AggregateGeneration is 0 or > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(fence));
    }
}
