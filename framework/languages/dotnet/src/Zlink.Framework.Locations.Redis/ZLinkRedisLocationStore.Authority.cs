using System.Buffers.Binary;
using System.Text;
using System.Text.Json;
using System.Security.Cryptography;
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
                        _keys.AuthorityOwnerLeaseGenerationsKey(),
                        _keys.AuthorityAllocationStatesKey(),
                        _keys.AuthorityAllocationsKey()
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
            string expectedStoreVersion,
            ZLinkAuthorityMutation mutation,
            CancellationToken cancellationToken = default)
    {
        ValidateAuthorityKey(key);
        ArgumentException.ThrowIfNullOrWhiteSpace(expectedStoreVersion);
        ArgumentNullException.ThrowIfNull(mutation);
        if (mutation is ZLinkAuthorityMutation.Put put)
        {
            ValidateAuthorityPayload(put.Payload);
            ValidateAuthorityMutation(put);
        }

        const string expectationName = "found";
        var expectedVersion = expectedStoreVersion;
        var mutationName = mutation is ZLinkAuthorityMutation.Delete
            ? "delete"
            : "put";
        var payload = mutation is ZLinkAuthorityMutation.Put value
            ? value.Payload.ToArray()
            : [];
        var transition = mutation is ZLinkAuthorityMutation.Put putValue
            ? putValue.GenerationTransition switch
            {
                ZLinkAuthorityGenerationTransition.Preserve => "preserve",
                ZLinkAuthorityGenerationTransition.NewOwner => "new-owner",
                _ => throw new ArgumentOutOfRangeException(nameof(mutation))
            }
            : string.Empty;
        var targetOwner = mutation is ZLinkAuthorityMutation.Put
            {
                TargetOwner: { } owner
            }
            ? owner
            : default;
        var validationOwner = targetOwner;
        if (validationOwner.OwnerId is null)
        {
            var current = await ReadAuthorityAsync(key, cancellationToken)
                .ConfigureAwait(false);
            if (current is ZLinkAuthorityReadResult.Found found)
                validationOwner = new ZLinkLocationOwnerToken(
                    found.Snapshot.OwnerId,
                    found.Snapshot.OwnerLeaseGeneration);
        }
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.CompareExchange,
                    AuthorityKeys(
                        validationOwner.OwnerId,
                        mutation is ZLinkAuthorityMutation.Put
                        {
                            RelocationCapacityFence: { } capacityFence
                        }
                            ? capacityFence.Value
                            : null),
                    [
                        key.Value,
                        expectationName,
                        expectedVersion,
                        mutationName,
                        payload,
                        transition,
                        validationOwner.OwnerId ?? string.Empty,
                        validationOwner.LeaseGeneration
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
            "invalid" => throw new InvalidOperationException(
                "Redis rejected an inconsistent authority mutation."),
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
                            _keys.AuthorityAllocationStatesKey(),
                            _keys.AuthorityAllocationsKey(),
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
        var entries = new List<ZLinkAuthorityEntry>(fields.Length / 9);
        for (var index = 0; index + 8 < fields.Length; index += 9)
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
                        _keys.AuthorityAllocationStatesKey(),
                        _keys.AuthorityAllocationsKey(),
                        _keys.AuthorityIndexKey(),
                        _keys.LeaseKey(request.TargetOwner.OwnerId),
                        _keys.AuthorityReservationKey(reservationVersion),
                        _keys.AuthorityPendingCapacityKey(),
                        DescriptorRowKey(request.TargetDescriptor)
                    ],
                    [
                        request.Key.Value,
                        request.CreatingPayload.ToArray(),
                        request.TargetOwner.OwnerId,
                        request.TargetOwner.LeaseGeneration,
                        request.TargetDescriptor.MeshName,
                        request.TargetDescriptor.Rid.ToHex(),
                        request.TargetNodeLifecycleGeneration,
                        (int)request.ObjectKind,
                        request.StableType,
                        EncodeAllocation(
                            request.ObjectKind,
                            request.StableType,
                            request.TargetDescriptor,
                            request.TargetNodeLifecycleGeneration,
                            request.PendingCapacityDelta),
                        request.PendingCapacityDelta
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
        if (status == "capacity-exhausted")
            return new ZLinkObjectReserveResult.PlacementCapacityExhausted();
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
                request.TargetNodeLifecycleGeneration,
                request.TargetOwner));
    }

    public async ValueTask<ZLinkRelocationCapacityReserveResult>
        ReserveRelocationCapacityAsync(
            ZLinkRelocationCapacityReservationRequest request,
            CancellationToken cancellationToken = default)
    {
        ValidateRelocationCapacityRequest(request);
        var fence = new ZLinkRelocationCapacityFence(
            request.ReservationId.ToString("N"));
        var signature = string.Join(
            "\n",
            request.Key.Value,
            request.ExpectedStoreVersion,
            (int)request.ObjectKind,
            request.StableType,
            request.SourceDescriptor.MeshName,
            request.SourceDescriptor.Rid.ToHex(),
            request.SourceNodeLifecycleGeneration,
            request.SourceOwner.OwnerId,
            request.SourceOwner.LeaseGeneration,
            request.TargetDescriptor.MeshName,
            request.TargetDescriptor.Rid.ToHex(),
            request.TargetNodeLifecycleGeneration,
            request.TargetOwner.OwnerId,
            request.TargetOwner.LeaseGeneration,
            request.CapacityDelta);
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.ReserveRelocationCapacity,
                    [
                        _keys.AuthorityVersionsKey(),
                        _keys.AuthorityOwnerIdsKey(),
                        _keys.AuthorityOwnerLeaseGenerationsKey(),
                        _keys.AuthorityAllocationStatesKey(),
                        _keys.AuthorityAllocationsKey(),
                        _keys.AuthorityRelocationCapacityKey(fence.Value),
                        _keys.LeaseKey(request.TargetOwner.OwnerId),
                        _keys.AuthorityPendingCapacityKey(),
                        DescriptorRowKey(request.TargetDescriptor)
                    ],
                    [
                        signature,
                        fence.Value,
                        request.Key.Value,
                        request.ExpectedStoreVersion,
                        request.SourceOwner.OwnerId,
                        request.SourceOwner.LeaseGeneration,
                        request.TargetOwner.OwnerId,
                        request.TargetOwner.LeaseGeneration,
                        EncodeAllocation(
                            request.ObjectKind,
                            request.StableType,
                            request.SourceDescriptor,
                            request.SourceNodeLifecycleGeneration,
                            request.CapacityDelta),
                        EncodeAllocation(
                            request.ObjectKind,
                            request.StableType,
                            request.TargetDescriptor,
                            request.TargetNodeLifecycleGeneration,
                            request.CapacityDelta),
                        request.TargetDescriptor.MeshName,
                        request.TargetDescriptor.Rid.ToHex(),
                        request.TargetNodeLifecycleGeneration,
                        (int)request.ObjectKind,
                        request.StableType,
                        request.CapacityDelta,
                        DescriptorRowKey(request.TargetDescriptor).ToString()
                    ]).ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        return (string)result[0]! switch
        {
            "reserved" => new ZLinkRelocationCapacityReserveResult.Reserved(
                fence),
            "already" => new ZLinkRelocationCapacityReserveResult.AlreadyReserved(
                fence),
            "target-unavailable" =>
                new ZLinkRelocationCapacityReserveResult.TargetUnavailable(),
            "conflict" => new ZLinkRelocationCapacityReserveResult.Conflict(
                await ReadAuthorityAsync(request.Key, cancellationToken)
                    .ConfigureAwait(false)),
            var status => throw new InvalidOperationException(
                $"Unknown relocation capacity reserve result '{status}'.")
        };
    }

    public async ValueTask<ZLinkRelocationCapacityAbortResult>
        AbortRelocationCapacityAsync(
            ZLinkRelocationCapacityFence fence,
            CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(fence.Value);
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.AbortRelocationCapacity,
                    [
                        _keys.AuthorityRelocationCapacityKey(fence.Value),
                        _keys.AuthorityPendingCapacityKey()
                    ],
                    []).ConfigureAwait(false))!,
                cancellationToken)
            .ConfigureAwait(false);
        return (string)result[0]! switch
        {
            "aborted" => ZLinkRelocationCapacityAbortResult.Aborted,
            "already" => ZLinkRelocationCapacityAbortResult.AlreadyAborted,
            "committed" =>
                ZLinkRelocationCapacityAbortResult.AlreadyCommitted,
            "stale" => ZLinkRelocationCapacityAbortResult.Stale,
            var status => throw new InvalidOperationException(
                $"Unknown relocation capacity abort result '{status}'.")
        };
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
                    ReservationKeys(reservation),
                    [
                        reservation.Key.Value,
                        reservation.StoreVersion,
                        readyPayload.ToArray(),
                        reservation.TargetDescriptor.MeshName,
                        reservation.TargetDescriptor.Rid.ToHex(),
                        reservation.TargetNodeLifecycleGeneration,
                        reservation.TargetOwner.OwnerId,
                        reservation.TargetOwner.LeaseGeneration
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
                    ReservationKeys(reservation),
                    [
                        reservation.Key.Value,
                        reservation.StoreVersion,
                        reservation.TargetDescriptor.MeshName,
                        reservation.TargetDescriptor.Rid.ToHex(),
                        reservation.TargetNodeLifecycleGeneration,
                        reservation.TargetOwner.OwnerId,
                        reservation.TargetOwner.LeaseGeneration
                    ])
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
            5 + request.Participants.Count * 5)
        {
            AggregateSignature(request),
            request.TargetOwner.OwnerId,
            request.TargetOwner.LeaseGeneration,
            request.Participants.Count,
            request.TargetReservations.Count
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
                    AggregateKeys(fence, request.TargetOwner.OwnerId)
                        .Concat(request.TargetReservations.Select(value =>
                            _keys.AuthorityRelocationCapacityKey(value.Value)))
                        .ToArray(),
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
        var targetOwnerId = await ExecuteAsync(
                async database => await database.HashGetAsync(
                        _keys.AuthorityAggregateKey(fence),
                        "targetOwner")
                    .ConfigureAwait(false),
                cancellationToken)
            .ConfigureAwait(false);
        if (targetOwnerId.IsNullOrEmpty)
            return ZLinkAggregateCommitResult.Stale;
        var result = await ExecuteAsync(
                async database => (RedisResult[])(await database.ScriptEvaluateAsync(
                    ZLinkRedisAuthorityScripts.CommitAggregate,
                    AggregateKeys(fence, (string)targetOwnerId!),
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
                    [
                        _keys.AuthorityAggregateKey(fence),
                        _keys.AuthorityPendingCapacityKey()
                    ],
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

    private RedisKey[] AuthorityKeys(
        string? targetOwnerId,
        string? relocationCapacityFence) =>
    [
        _keys.AuthorityCountersKey(),
        _keys.AuthorityVersionsKey(),
        _keys.AuthorityPayloadsKey(),
        _keys.AuthorityObjectGenerationsKey(),
        _keys.AuthorityOwnerGenerationsKey(),
        _keys.AuthorityOwnerIdsKey(),
        _keys.AuthorityOwnerLeaseGenerationsKey(),
        _keys.AuthorityAllocationStatesKey(),
        _keys.AuthorityAllocationsKey(),
        _keys.AuthorityMembershipsKey(),
        _keys.AuthorityIndexKey(),
        string.IsNullOrEmpty(targetOwnerId)
            ? _keys.LeaseKey("__unused__")
            : _keys.LeaseKey(targetOwnerId),
        string.IsNullOrEmpty(relocationCapacityFence)
            ? _keys.AuthorityRelocationCapacityKey("__unused__")
            : _keys.AuthorityRelocationCapacityKey(
                relocationCapacityFence),
        _keys.AuthorityActiveCapacityKey(),
        _keys.AuthorityPendingCapacityKey()
    ];

    private RedisKey[] ReservationKeys(ZLinkObjectReservation reservation) =>
    [
        _keys.AuthorityCountersKey(),
        _keys.AuthorityVersionsKey(),
        _keys.AuthorityPayloadsKey(),
        _keys.AuthorityObjectGenerationsKey(),
        _keys.AuthorityOwnerGenerationsKey(),
        _keys.AuthorityOwnerIdsKey(),
        _keys.AuthorityOwnerLeaseGenerationsKey(),
        _keys.AuthorityAllocationStatesKey(),
        _keys.AuthorityAllocationsKey(),
        _keys.AuthorityIndexKey(),
        _keys.LeaseKey(reservation.TargetOwner.OwnerId),
        _keys.AuthorityReservationKey(reservation.ReservationVersion),
        _keys.AuthorityActiveCapacityKey(),
        _keys.AuthorityPendingCapacityKey(),
        DescriptorRowKey(reservation.TargetDescriptor)
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
        _keys.AuthorityAllocationStatesKey(),
        _keys.AuthorityAllocationsKey(),
        _keys.AuthorityMembershipsKey(),
        _keys.AuthorityCountersKey(),
        _keys.AuthorityAggregateKey(fence),
        string.IsNullOrEmpty(ownerId)
            ? _keys.LeaseKey("__unused__")
            : _keys.LeaseKey(ownerId),
        _keys.AuthorityActiveCapacityKey(),
        _keys.AuthorityPendingCapacityKey()
    ];

    private RedisKey DescriptorRowKey(
        ZLinkMeshNodeDescriptorKey descriptor) =>
        _keys.RowHashKey(
            ZLinkRedisLocationKinds.MeshNode.Tag,
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(descriptor));

    private static string AggregateSignature(
        ZLinkAggregatePrepareRequest request)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        AppendSignature(hash, request.AggregateId.ToByteArray());
        AppendSignature(hash, request.AggregateGeneration);
        AppendSignature(hash, request.TargetOwner.OwnerId);
        AppendSignature(hash, request.TargetOwner.LeaseGeneration);
        AppendSignature(hash, request.InventoryDigest.Span);
        AppendSignature(hash, request.TargetReservations.Count);
        foreach (var reservation in request.TargetReservations)
            AppendSignature(hash, reservation.Value);
        AppendSignature(hash, request.Participants.Count);
        foreach (var participant in request.Participants)
        {
            AppendSignature(hash, participant.Key.Value);
            AppendSignature(hash, participant.ExpectedStoreVersion);
            AppendSignature(hash, (int)participant.OwnerTransition);
            AppendSignature(hash, participant.AuthorityPayload.Span);
            AppendSignature(hash, participant.MembershipMutation.Span);
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }

    private static void AppendSignature(
        IncrementalHash hash,
        string value) =>
        AppendSignature(hash, Encoding.UTF8.GetBytes(value));

    private static void AppendSignature(
        IncrementalHash hash,
        long value)
    {
        Span<byte> bytes = stackalloc byte[sizeof(long)];
        BinaryPrimitives.WriteInt64BigEndian(bytes, value);
        AppendSignature(hash, bytes);
    }

    private static void AppendSignature(
        IncrementalHash hash,
        ulong value)
    {
        Span<byte> bytes = stackalloc byte[sizeof(ulong)];
        BinaryPrimitives.WriteUInt64BigEndian(bytes, value);
        AppendSignature(hash, bytes);
    }

    private static void AppendSignature(
        IncrementalHash hash,
        int value)
    {
        Span<byte> bytes = stackalloc byte[sizeof(int)];
        BinaryPrimitives.WriteInt32BigEndian(bytes, value);
        AppendSignature(hash, bytes);
    }

    private static void AppendSignature(
        IncrementalHash hash,
        ReadOnlySpan<byte> value)
    {
        Span<byte> length = stackalloc byte[sizeof(int)];
        BinaryPrimitives.WriteInt32BigEndian(length, value.Length);
        hash.AppendData(length);
        hash.AppendData(value);
    }

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
            DecodeAllocation(
                (string)values[offset + 6]!,
                ToBytes(values[offset + 7])),
            storeNow);

    private static byte[] EncodeAllocation(
        ZLinkPlacementObjectKind objectKind,
        string stableType,
        ZLinkMeshNodeDescriptorKey descriptor,
        ulong descriptorLifecycleGeneration,
        int capacityDelta) =>
        JsonSerializer.SerializeToUtf8Bytes(
            new AllocationEnvelope(
                (int)objectKind,
                stableType,
                descriptor.MeshName,
                descriptor.Rid.ToHex(),
                descriptorLifecycleGeneration,
                capacityDelta));

    private static ZLinkPlacementAllocation DecodeAllocation(
        string state,
        byte[] encoded)
    {
        var value = JsonSerializer.Deserialize<AllocationEnvelope>(encoded)
                    ?? throw new InvalidDataException(
                        "Redis authority allocation was unexpectedly empty.");
        return new ZLinkPlacementAllocation(
            state switch
            {
                "pending" => ZLinkPlacementAllocationState.Pending,
                "active" => ZLinkPlacementAllocationState.Active,
                _ => throw new InvalidDataException(
                    $"Redis authority allocation state '{state}' is invalid.")
            },
            (ZLinkPlacementObjectKind)value.ObjectKind,
            value.StableType,
            new ZLinkMeshNodeDescriptorKey(
                value.MeshName,
                RoutingId.FromHex(value.Rid)),
            value.DescriptorLifecycleGeneration,
            value.CapacityDelta);
    }

    private static byte[] ToBytes(RedisResult result) =>
        (byte[]?)(RedisValue)result!
        ?? throw new InvalidDataException(
            "Redis authority payload was unexpectedly null.");

    private static ulong ParseGeneration(RedisResult result) =>
        ulong.Parse((string)result!, System.Globalization.CultureInfo.InvariantCulture);

    private sealed record AllocationEnvelope(
        int ObjectKind,
        string StableType,
        string MeshName,
        string Rid,
        ulong DescriptorLifecycleGeneration,
        int CapacityDelta);

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

    private static void ValidateAuthorityMutation(
        ZLinkAuthorityMutation.Put put)
    {
        var preserve =
            put.GenerationTransition == ZLinkAuthorityGenerationTransition.Preserve;
        var newOwner =
            put.GenerationTransition == ZLinkAuthorityGenerationTransition.NewOwner;
        if (!preserve && !newOwner
            || preserve && (put.TargetOwner is not null
                            || put.RelocationCapacityFence is not null)
            || newOwner && (put.TargetOwner is null
                            || put.RelocationCapacityFence is null)
            || put.TargetOwner is { } owner
            && (string.IsNullOrWhiteSpace(owner.OwnerId)
                || owner.LeaseGeneration <= 0))
            throw new ArgumentException(
                "Authority mutation transition, expectation, and target owner are inconsistent.",
                nameof(put));
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
            || request.CreatingPayload.Length > 1024 * 1024
            || request.PendingCapacityDelta <= 0
            || request.TargetOwner.LeaseGeneration <= 0)
            throw new ArgumentOutOfRangeException(nameof(request));
    }

    private static void ValidateRelocationCapacityRequest(
        ZLinkRelocationCapacityReservationRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (request.ReservationId == Guid.Empty
            || string.IsNullOrWhiteSpace(request.Key.Value)
            || string.IsNullOrWhiteSpace(request.ExpectedStoreVersion)
            || string.IsNullOrWhiteSpace(request.StableType)
            || request.SourceOwner.LeaseGeneration <= 0
            || request.TargetOwner.LeaseGeneration <= 0
            || request.CapacityDelta <= 0)
            throw new ArgumentException(
                "The relocation capacity reservation is invalid.",
                nameof(request));
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
        var participantKeys =
            request.Participants.Select(static value => value.Key.Value)
                .ToArray();
        if (participantKeys.Distinct(StringComparer.Ordinal).Count()
            != participantKeys.Length
            || !participantKeys.SequenceEqual(
                participantKeys.OrderBy(
                    static value => value,
                    StringComparer.Ordinal),
                StringComparer.Ordinal))
            throw new ArgumentException(
                "Aggregate participant keys must be unique and canonically sorted.",
                nameof(request));
    }

    private static void ValidateFence(ZLinkAggregateFence fence)
    {
        if (fence.AggregateId == Guid.Empty
            || fence.AggregateGeneration is 0 or > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(fence));
    }
}
