using System.Buffers.Binary;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
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
                database => AuthorityCallAsync(
                    database,
                    "read",
                    key.Value,
                    new { key = key.Value }),
                cancellationToken)
            .ConfigureAwait(false);
        var now = DateTimeOffset.FromUnixTimeMilliseconds(
            result.GetProperty("storeNowMs").GetInt64());
        return result.GetProperty("kind").GetString() == "missing"
            ? new ZLinkAuthorityReadResult.Missing(now)
            : new ZLinkAuthorityReadResult.Found(
                Snapshot(result, now));
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
        var fenceValue = mutation is ZLinkAuthorityMutation.Put
        {
            RelocationCapacityFence: { } capacityFence
        }
            ? capacityFence.Value
            : null;
        var result = await ExecuteAsync(
                async database =>
                {
                    RedisKey? descriptor = null;
                    RedisKey? admission = null;
                    RedisKey? targetLease = targetOwner.OwnerId is null
                        ? (RedisKey?)null
                        : _keys.HybridOwnerLeaseKey(targetOwner.OwnerId);
                    RedisKey? record = string.IsNullOrEmpty(fenceValue)
                        ? (RedisKey?)null
                        : _keys.HybridRelocationKey(fenceValue);
                    if (record is { } relocationKey)
                    {
                        var requestJson = await database.HashGetAsync(
                                relocationKey,
                                "requestJson")
                            .ConfigureAwait(false);
                        if (!requestJson.IsNull)
                        {
                            using var document = JsonDocument.Parse(
                                requestJson.ToString());
                            var comparable = document.RootElement;
                            var targetDescriptorKey = comparable
                                .GetProperty("targetDescriptorKey")
                                .GetString()!;
                            descriptor = _keys.HybridDescriptorKey(
                                targetDescriptorKey);
                            admission =
                                _keys.HybridDescriptorAdmissionKey(
                                    targetDescriptorKey);
                        }
                    }
                    return await AuthorityCallAsync(
                            database,
                            "cas",
                            key.Value,
                            new
                            {
                                key = key.Value,
                                expectedStoreVersion = expectedVersion,
                                mutationKind = mutationName,
                                transition = transition switch
                                {
                                    "new-owner" => "newOwner",
                                    _ => transition
                                },
                                payload = Convert.ToBase64String(payload),
                                targetOwner = targetOwner.OwnerId is null
                                    ? null
                                    : OwnerJson(targetOwner),
                                fence = fenceValue,
                                currentOwner =
                                    validationOwner.OwnerId is null
                                        ? null
                                        : OwnerJson(validationOwner)
                            },
                            descriptor,
                            admission,
                            targetLease,
                            record,
                            validationOwner.OwnerId is null
                                ? (RedisKey?)null
                                : _keys.HybridOwnerLeaseKey(
                                    validationOwner.OwnerId))
                        .ConfigureAwait(false);
                },
                cancellationToken)
            .ConfigureAwait(false);
        var status = result.GetProperty("kind").GetString();
        var now = result.TryGetProperty("storeNowMs", out var nowValue)
            ? DateTimeOffset.FromUnixTimeMilliseconds(nowValue.GetInt64())
            : default;
        return status switch
        {
            "stored" => new ZLinkAuthorityCompareExchangeResult.Stored(
                Snapshot(result, now)),
            "deleted" => new ZLinkAuthorityCompareExchangeResult.Deleted(
                result.GetProperty("storeVersion").GetString()!,
                now),
            "conflict" => new ZLinkAuthorityCompareExchangeResult.Conflict(
                AuthorityRead(result.GetProperty("current"))),
            "generationExhausted" =>
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
        string lastHex;
        if (cursor is null)
        {
            scanId = Guid.NewGuid().ToString("N");
            lastHex = string.Empty;
            var started = await ExecuteAsync(
                    database => AuthorityCallAsync(
                        database,
                        "scan",
                        string.Empty,
                        new
                        {
                            start = true,
                            scanId,
                            prefix,
                            retentionMs = (long)AuthorityScanRetention
                                .TotalMilliseconds
                        },
                        extraKeys: [_keys.HybridScanKey(scanId)]),
                    cancellationToken)
                .ConfigureAwait(false);
            if (started.GetProperty("kind").GetString() != "started")
                throw new InvalidOperationException(
                    "Redis authority scan did not start.");
        }
        else
        {
            (scanId, lastHex) = ParseCursor(cursor.Value);
        }

        return await ExecuteAsync<ZLinkAuthorityScanResult>(
                async database =>
                {
                    var lowerBound = string.IsNullOrEmpty(lastHex)
                        ? "-"
                        : $"({lastHex}";
                    var candidateResults = (RedisResult[])(await database
                        .ExecuteAsync(
                            "ZRANGEBYLEX",
                            _keys.HybridAuthorityKeyIndexKey(),
                            lowerBound,
                            "+",
                            "LIMIT",
                            0,
                            1000)
                        .ConfigureAwait(false))!;
                    var candidates = candidateResults
                        .Select(static value => (string)value!)
                        .ToArray();
                    var pageKeys = new List<RedisKey>(
                        1 + candidates.Length * 3)
                    {
                        _keys.HybridScanKey(scanId)
                    };
                    foreach (var candidate in candidates)
                    {
                        var authorityKey = Encoding.UTF8.GetString(
                            Convert.FromHexString(candidate));
                        pageKeys.Add(
                            _keys.HybridAuthorityCurrentKey(authorityKey));
                        pageKeys.Add(
                            _keys.HybridAuthorityHistoryKey(authorityKey));
                        pageKeys.Add(
                            _keys.HybridAuthorityHistoryRevisionsKey(
                                authorityKey));
                    }
                    var result = await AuthorityCallAsync(
                            database,
                            "scan",
                            string.Empty,
                            new
                            {
                                start = false,
                                scanId,
                                prefix,
                                expectedLastHex = lastHex,
                                candidates,
                                dynamicStart = 21,
                                limit,
                                retentionMs = (long)AuthorityScanRetention
                                    .TotalMilliseconds
                            },
                            extraKeys: pageKeys)
                        .ConfigureAwait(false);
                    if (result.GetProperty("kind").GetString()
                        == "scanExpired")
                        return new ZLinkAuthorityScanResult.ScanExpired();

                    var now = DateTimeOffset.FromUnixTimeMilliseconds(
                        result.GetProperty("storeNowMs").GetInt64());
                    var entries = result.GetProperty("rows")
                        .EnumerateArray()
                        .Select(row => new ZLinkAuthorityEntry(
                            new ZLinkAuthorityKey(
                                row.GetProperty("key").GetString()!),
                            Snapshot(row.GetProperty("row"), now)))
                        .ToArray();
                    ZLinkAuthorityScanCursor? next =
                        result.GetProperty("hasMore").GetBoolean()
                        ? new ZLinkAuthorityScanCursor(
                            $"{scanId}:{result.GetProperty("lastHex").GetString()}")
                        : null;
                    return new ZLinkAuthorityScanResult.Page(
                        new ZLinkAuthorityPage(entries, next));
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkObjectReserveResult> ReserveAsync(
        ZLinkObjectReservationRequest request,
        CancellationToken cancellationToken = default)
    {
        ValidateReservationRequest(request);
        var reservationVersion = Guid.NewGuid().ToString("N");
        var descriptorKey = ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
            request.TargetDescriptor);
        var target = new
        {
            descriptor = new
            {
                meshName = request.TargetDescriptor.MeshName,
                rid = request.TargetDescriptor.Rid.ToHex()
            },
            descriptorKey,
            lifecycleGeneration =
                request.TargetNodeLifecycleGeneration.ToString(
                    System.Globalization.CultureInfo.InvariantCulture),
            owner = OwnerJson(request.TargetOwner)
        };
        RedisKey entrySpotClaimKey = _keys.HybridSchemaKey();
        if (request.ObjectKind is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot
            && Zlink.Framework.Runtime.Spots
                .ZLinkUserSpotAuthorityPayloadCodec.TryGetSpotId(
                    request.Key,
                    out var spotId))
            entrySpotClaimKey =
                _keys.HybridEntrySpotIdClaimKey(spotId);
        var result = await ExecuteAsync(
                database => AuthorityCallAsync(
                    database,
                    "reserve",
                    request.Key.Value,
                    new
                    {
                        key = request.Key.Value,
                        objectKind = ObjectKindToken(request.ObjectKind),
                        checkEntrySpotClaim =
                            request.ObjectKind is
                                ZLinkPlacementObjectKind.UserSpot
                                or ZLinkPlacementObjectKind.InstanceSpot,
                        stableType = request.StableType,
                        capacity = CapacityJson(request.Capacity),
                        capacityBundle = EncodeCapacityBundle(request.Capacity),
                        payload = Convert.ToBase64String(
                            request.CreatingPayload.Span),
                        reservationId = reservationVersion,
                        intent = new
                        {
                            requestContentReference =
                                request.CreationIntentReference,
                            requestSha256 = Convert.ToBase64String(
                                request.CreationIntentHash.Span),
                            requestEncodedSize =
                                request.CreationIntentEncodedSize.ToString(
                                    System.Globalization.CultureInfo
                                        .InvariantCulture)
                        },
                        target
                    },
                    HybridDescriptorRowKey(request.TargetDescriptor),
                    _keys.HybridDescriptorAdmissionKey(descriptorKey),
                    _keys.HybridOwnerLeaseKey(
                        request.TargetOwner.OwnerId),
                    _keys.HybridCreationKey(reservationVersion),
                    extraKeys: [entrySpotClaimKey]),
                cancellationToken)
            .ConfigureAwait(false);
        var status = result.GetProperty("kind").GetString();
        var now = result.TryGetProperty("storeNowMs", out var nowValue)
            ? DateTimeOffset.FromUnixTimeMilliseconds(nowValue.GetInt64())
            : default;
        if (status == "alreadyExists")
        {
            return new ZLinkObjectReserveResult.AlreadyExists(
                Snapshot(result.GetProperty("current"), now));
        }
        if (status == "typeMismatch")
            return new ZLinkObjectReserveResult.TypeMismatch(
                Snapshot(result.GetProperty("current"), now));
        if (status == "conflict")
        {
            return new ZLinkObjectReserveResult.Conflict(
                AuthorityRead(result.GetProperty("current")));
        }
        if (status == "targetUnavailable")
        {
            return new ZLinkObjectReserveResult.Conflict(
                AuthorityRead(result.GetProperty("current")));
        }
        if (status == "generationExhausted")
            return new ZLinkObjectReserveResult.GenerationExhausted();
        if (status == "placementCapacityExhausted")
            return new ZLinkObjectReserveResult.PlacementCapacityExhausted();
        if (status != "reserved")
            throw new InvalidOperationException(
                $"Unknown Redis authority reserve result '{status}'.");

        return new ZLinkObjectReserveResult.Reserved(
            new ZLinkObjectReservation(
                request.Key,
                result.GetProperty("creating")
                    .GetProperty("storeVersion").GetString()!,
                ulong.Parse(
                    result.GetProperty("creating")
                        .GetProperty("objectGeneration").GetString()!),
                ulong.Parse(
                    result.GetProperty("creating")
                        .GetProperty("authorityOwnerGeneration").GetString()!),
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
        var sourceDescriptorKey =
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
                request.SourceDescriptor);
        var targetDescriptorKey =
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
                request.TargetDescriptor);
        var comparable = new
        {
            reservationId = fence.Value,
            key = request.Key.Value,
            expectedStoreVersion = request.ExpectedStoreVersion,
            objectKind = ObjectKindToken(request.ObjectKind),
            stableType = request.StableType,
            sourceDescriptor = new
            {
                meshName = request.SourceDescriptor.MeshName,
                rid = request.SourceDescriptor.Rid.ToHex()
            },
            sourceDescriptorKey,
            sourceNodeLifecycleGeneration =
                request.SourceNodeLifecycleGeneration.ToString(
                    System.Globalization.CultureInfo.InvariantCulture),
            sourceOwner = OwnerJson(request.SourceOwner),
            targetDescriptor = new
            {
                meshName = request.TargetDescriptor.MeshName,
                rid = request.TargetDescriptor.Rid.ToHex()
            },
            targetDescriptorKey,
            targetNodeLifecycleGeneration =
                request.TargetNodeLifecycleGeneration.ToString(
                    System.Globalization.CultureInfo.InvariantCulture),
            targetOwner = OwnerJson(request.TargetOwner),
            capacity = CapacityJson(request.Capacity),
            capacityBundle = EncodeCapacityBundle(request.Capacity)
        };
        var result = await ExecuteAsync(
                database => AuthorityCallAsync(
                    database,
                    "reserveRelocation",
                    request.Key.Value,
                    new
                    {
                        comparable.reservationId,
                        comparable.key,
                        comparable.expectedStoreVersion,
                        comparable.objectKind,
                        comparable.stableType,
                        comparable.sourceDescriptor,
                        comparable.sourceDescriptorKey,
                        comparable.sourceNodeLifecycleGeneration,
                        comparable.sourceOwner,
                        comparable.targetDescriptor,
                        comparable.targetDescriptorKey,
                        comparable.targetNodeLifecycleGeneration,
                        comparable.targetOwner,
                        comparable.capacity,
                        comparable.capacityBundle,
                        requestJson = JsonSerializer.Serialize(comparable),
                        target = new
                        {
                            descriptor = comparable.targetDescriptor,
                            descriptorKey = targetDescriptorKey,
                            lifecycleGeneration =
                                comparable.targetNodeLifecycleGeneration,
                            owner = comparable.targetOwner
                        }
                    },
                    HybridDescriptorRowKey(request.TargetDescriptor),
                    _keys.HybridDescriptorAdmissionKey(
                        targetDescriptorKey),
                    _keys.HybridOwnerLeaseKey(
                        request.TargetOwner.OwnerId),
                    _keys.HybridRelocationKey(fence.Value),
                    _keys.HybridOwnerLeaseKey(
                        request.SourceOwner.OwnerId)),
                cancellationToken)
            .ConfigureAwait(false);
        return result.GetProperty("kind").GetString() switch
        {
            "reserved" => new ZLinkRelocationCapacityReserveResult.Reserved(
                fence),
            "alreadyReserved" =>
                new ZLinkRelocationCapacityReserveResult.AlreadyReserved(
                fence),
            "targetUnavailable" =>
                new ZLinkRelocationCapacityReserveResult.TargetUnavailable(),
            "placementCapacityExhausted" =>
                new ZLinkRelocationCapacityReserveResult
                    .PlacementCapacityExhausted(),
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
                database => AuthorityCallAsync(
                    database,
                    "abortRelocation",
                    string.Empty,
                    new { fence = fence.Value },
                    recordKey: _keys.HybridRelocationKey(fence.Value)),
                cancellationToken)
            .ConfigureAwait(false);
        return result.GetProperty("kind").GetString() switch
        {
            "aborted" => ZLinkRelocationCapacityAbortResult.Aborted,
            "alreadyAborted" =>
                ZLinkRelocationCapacityAbortResult.AlreadyAborted,
            "alreadyCommitted" =>
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
        var descriptorKey = ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
            reservation.TargetDescriptor);
        var target = new
        {
            descriptor = new
            {
                meshName = reservation.TargetDescriptor.MeshName,
                rid = reservation.TargetDescriptor.Rid.ToHex()
            },
            descriptorKey,
            lifecycleGeneration =
                reservation.TargetNodeLifecycleGeneration.ToString(
                    System.Globalization.CultureInfo.InvariantCulture),
            owner = OwnerJson(reservation.TargetOwner)
        };
        var result = await ExecuteAsync(
                database => AuthorityCallAsync(
                    database,
                    "commit",
                    reservation.Key.Value,
                    new
                    {
                        key = reservation.Key.Value,
                        reservationId = reservation.ReservationVersion,
                        expectedStoreVersion = reservation.StoreVersion,
                        target,
                        payload = Convert.ToBase64String(readyPayload.Span)
                    },
                    HybridDescriptorRowKey(reservation.TargetDescriptor),
                    _keys.HybridDescriptorAdmissionKey(descriptorKey),
                    _keys.HybridOwnerLeaseKey(
                        reservation.TargetOwner.OwnerId),
                    _keys.HybridCreationKey(
                        reservation.ReservationVersion)),
                cancellationToken)
            .ConfigureAwait(false);
        var status = result.GetProperty("kind").GetString();
        var now = result.TryGetProperty("ready", out var ready)
            ? DateTimeOffset.FromUnixTimeMilliseconds(
                ready.GetProperty("storeNowMs").GetInt64())
            : default;
        return status switch
        {
            "committed" => new ZLinkObjectCommitResult.Committed(
                Snapshot(result.GetProperty("ready"), now)),
            "alreadyCommitted" => new ZLinkObjectCommitResult.AlreadyCommitted(
                Snapshot(result.GetProperty("ready"), now)),
            "stale" => new ZLinkObjectCommitResult.Stale(),
            "generationExhausted" =>
                new ZLinkObjectCommitResult.GenerationExhausted(),
            _ => throw new InvalidOperationException(
                $"Unknown Redis authority commit result '{status}'.")
        };
    }

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
            ZLinkObjectCreationCompletion.Created created => created.Terminal,
            ZLinkObjectCreationCompletion.Rejected rejected => rejected.Terminal,
            ZLinkObjectCreationCompletion.Failed failed => failed.Terminal,
            _ => throw new ArgumentOutOfRangeException(nameof(completion))
        };
        ValidateCreationTerminal(publication);
        var readyPayload = completion is ZLinkObjectCreationCompletion.Created accepted
            ? accepted.ReadyPayload
            : ReadOnlyMemory<byte>.Empty;
        if (completion is ZLinkObjectCreationCompletion.Created)
            ValidateAuthorityPayload(readyPayload);
        var state = completion switch
        {
            ZLinkObjectCreationCompletion.Created => "Created",
            ZLinkObjectCreationCompletion.Rejected => "Rejected",
            ZLinkObjectCreationCompletion.Failed => "Failed",
            _ => throw new ArgumentOutOfRangeException(nameof(completion))
        };
        var descriptorKey = ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
            reservation.TargetDescriptor);
        var target = new
        {
            descriptor = new
            {
                meshName = reservation.TargetDescriptor.MeshName,
                rid = reservation.TargetDescriptor.Rid.ToHex()
            },
            descriptorKey,
            lifecycleGeneration =
                reservation.TargetNodeLifecycleGeneration.ToString(
                    System.Globalization.CultureInfo.InvariantCulture),
            owner = OwnerJson(reservation.TargetOwner)
        };
        var operation = publication.Operation;
        var result = await ExecuteAsync(
                database => AuthorityCallAsync(
                    database,
                    "completeCreation",
                    reservation.Key.Value,
                    new
                    {
                        key = reservation.Key.Value,
                        reservationId = reservation.ReservationVersion,
                        expectedStoreVersion = reservation.StoreVersion,
                        target,
                        payload = Convert.ToBase64String(readyPayload.Span),
                        terminal = new
                        {
                            state,
                            sourceNodeRid = operation.SourceNodeRid.ToHex(),
                            sourceNodeGeneration =
                                operation.SourceNodeGeneration.ToString(
                                    System.Globalization.CultureInfo.InvariantCulture),
                            operationIdHigh = operation.OperationIdHigh.ToString(
                                System.Globalization.CultureInfo.InvariantCulture),
                            operationIdLow = operation.OperationIdLow.ToString(
                                System.Globalization.CultureInfo.InvariantCulture),
                            terminalEnvelope = Convert.ToHexString(
                                    publication.TerminalEnvelope.Span)
                                .ToLowerInvariant(),
                            terminalEnvelopeSha256 = Convert.ToHexString(
                                    publication.TerminalEnvelopeSha256.Span)
                                .ToLowerInvariant(),
                            expiresAtUnixMs = publication.ExpiresAt
                                .ToUnixTimeMilliseconds()
                                .ToString(
                                    System.Globalization.CultureInfo.InvariantCulture)
                        }
                    },
                    HybridDescriptorRowKey(reservation.TargetDescriptor),
                    _keys.HybridDescriptorAdmissionKey(descriptorKey),
                    _keys.HybridOwnerLeaseKey(
                        reservation.TargetOwner.OwnerId),
                    _keys.HybridCreationKey(
                        reservation.ReservationVersion),
                    extraKeys:
                    [
                        _keys.HybridCreationTerminalKey(operation)
                    ]),
                cancellationToken)
            .ConfigureAwait(false);
        var kind = result.GetProperty("kind").GetString();
        if (kind == "invalidExpiry")
            throw new ArgumentOutOfRangeException(
                nameof(completion),
                "The creation terminal must expire after provider StoreNow.");
        if (kind is "stale")
            return new ZLinkObjectCreationCompleteResult.Stale();
        if (kind is "generationExhausted")
            return new ZLinkObjectCreationCompleteResult.GenerationExhausted();
        var now = DateTimeOffset.FromUnixTimeMilliseconds(
            result.GetProperty("storeNowMs").GetInt64());
        var terminal = CreationTerminal(
            publication.Operation,
            result.GetProperty("terminal"),
            now);
        return kind switch
        {
            "alreadyCompleted" =>
                new ZLinkObjectCreationCompleteResult.AlreadyCompleted(terminal),
            "created" => new ZLinkObjectCreationCompleteResult.Created(
                Snapshot(result.GetProperty("ready"), now),
                terminal),
            "rejected" =>
                new ZLinkObjectCreationCompleteResult.Rejected(terminal),
            "failed" => new ZLinkObjectCreationCompleteResult.Failed(terminal),
            _ => throw new InvalidOperationException(
                $"Unknown Redis creation completion result '{kind}'.")
        };
    }

    public async ValueTask<ZLinkCreationTerminalReadResult>
        ReadCreationTerminalAsync(
            ZLinkCreationOperationId operation,
            CancellationToken cancellationToken = default)
    {
        ValidateCreationOperation(operation);
        var result = await ExecuteAsync(
                database => AuthorityCallAsync(
                    database,
                    "readCreationTerminal",
                    string.Empty,
                    new { },
                    recordKey: _keys.HybridCreationTerminalKey(operation)),
                cancellationToken)
            .ConfigureAwait(false);
        var now = DateTimeOffset.FromUnixTimeMilliseconds(
            result.GetProperty("storeNowMs").GetInt64());
        return result.GetProperty("kind").GetString() == "missing"
            ? new ZLinkCreationTerminalReadResult.Missing(now)
            : new ZLinkCreationTerminalReadResult.Found(
                CreationTerminal(
                    operation,
                    result.GetProperty("terminal"),
                    now));
    }

    public async ValueTask<ZLinkObjectAbortResult> AbortAsync(
        ZLinkObjectReservation reservation,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(reservation);
        var descriptorKey = ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
            reservation.TargetDescriptor);
        var result = await ExecuteAsync(
                database => AuthorityCallAsync(
                    database,
                    "abort",
                    reservation.Key.Value,
                    new
                    {
                        key = reservation.Key.Value,
                        reservationId = reservation.ReservationVersion,
                        expectedStoreVersion = reservation.StoreVersion,
                        target = new
                        {
                            descriptor = new
                            {
                                meshName =
                                    reservation.TargetDescriptor.MeshName,
                                rid = reservation.TargetDescriptor.Rid.ToHex()
                            },
                            descriptorKey,
                            lifecycleGeneration =
                                reservation.TargetNodeLifecycleGeneration
                                    .ToString(
                                        System.Globalization.CultureInfo
                                            .InvariantCulture),
                            owner = OwnerJson(reservation.TargetOwner)
                        }
                    },
                    HybridDescriptorRowKey(reservation.TargetDescriptor),
                    _keys.HybridDescriptorAdmissionKey(descriptorKey),
                    _keys.HybridOwnerLeaseKey(
                        reservation.TargetOwner.OwnerId),
                    _keys.HybridCreationKey(
                        reservation.ReservationVersion)),
                cancellationToken)
            .ConfigureAwait(false);
        return result.GetProperty("kind").GetString() switch
        {
            "aborted" => new ZLinkObjectAbortResult.Aborted(),
            "alreadyAborted" => new ZLinkObjectAbortResult.AlreadyAborted(),
            "stale" => new ZLinkObjectAbortResult.Stale(),
            "generationExhausted" =>
                new ZLinkObjectAbortResult.GenerationExhausted(),
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
        var comparable = new
        {
            aggregateId = request.AggregateId.ToString("D"),
            aggregateGeneration = request.AggregateGeneration.ToString(
                System.Globalization.CultureInfo.InvariantCulture),
            participants = request.Participants.Select(participant => new
            {
                key = participant.Key.Value,
                keyHex = Convert.ToHexString(
                        Encoding.UTF8.GetBytes(participant.Key.Value))
                    .ToLowerInvariant(),
                expectedStoreVersion =
                    participant.ExpectedStoreVersion,
                ownerTransition = participant.OwnerTransition ==
                    ZLinkAuthorityGenerationTransition.Preserve
                        ? "preserve"
                        : "newOwner",
                authorityPayload = Convert.ToBase64String(
                    participant.AuthorityPayload.Span),
                membershipMutation = Convert.ToBase64String(
                    participant.MembershipMutation.Span)
            }).ToArray(),
            inventoryDigest = Convert.ToBase64String(
                request.InventoryDigest.Span),
            target = new
            {
                descriptor = new
                {
                    meshName = request.TargetDescriptor.MeshName,
                    rid = request.TargetDescriptor.Rid.ToHex()
                },
                descriptorKey = ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
                    request.TargetDescriptor),
                lifecycleGeneration =
                    request.TargetDescriptorLifecycleGeneration.ToString(
                        System.Globalization.CultureInfo.InvariantCulture),
                owner = OwnerJson(request.TargetOwner)
            },
            capacity = CapacityJson(request.Capacity),
            capacityBundle = EncodeCapacityBundle(request.Capacity),
            targetOwner = OwnerJson(request.TargetOwner)
        };
        var aggregateKey = _keys.HybridAggregateKey(
            fence.AggregateId,
            fence.AggregateGeneration);
        var requestValue = new
        {
            comparable.aggregateId,
            comparable.aggregateGeneration,
            comparable.participants,
            comparable.inventoryDigest,
            comparable.target,
            comparable.capacity,
            comparable.capacityBundle,
            comparable.targetOwner,
            aggregateKey =
                $"{request.AggregateId:D}:{request.AggregateGeneration}",
            fence = new
            {
                aggregateId = request.AggregateId.ToString("D"),
                aggregateGeneration =
                    request.AggregateGeneration.ToString(
                        System.Globalization.CultureInfo.InvariantCulture)
            },
            requestJson = JsonSerializer.Serialize(comparable)
        };
        using var requestDocument = JsonDocument.Parse(
            JsonSerializer.Serialize(requestValue));

        var result = await ExecuteAsync(
                async database => await AuthorityCallAsync(
                        database,
                        "prepareAggregate",
                        string.Empty,
                        requestValue,
                        recordKey: aggregateKey,
                        extraKeys: await BuildAggregateExtraKeysAsync(
                                database,
                                requestDocument.RootElement,
                                commit: false,
                                abort: false)
                            .ConfigureAwait(false))
                    .ConfigureAwait(false),
                cancellationToken)
            .ConfigureAwait(false);
        return result.GetProperty("kind").GetString() switch
        {
            "prepared" => new ZLinkAggregatePrepareResult.Prepared(fence),
            "alreadyPrepared" =>
                new ZLinkAggregatePrepareResult.AlreadyPrepared(fence),
            "conflict" => new ZLinkAggregatePrepareResult.Conflict(),
            "stale" => new ZLinkAggregatePrepareResult.Stale(),
            "generationExhausted" =>
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
        var aggregateKey = _keys.HybridAggregateKey(
            fence.AggregateId,
            fence.AggregateGeneration);
        var result = await ExecuteAsync(
                async database =>
                {
                    var json = await database.HashGetAsync(
                            aggregateKey,
                            "requestJson")
                        .ConfigureAwait(false);
                    if (json.IsNull)
                        return default;
                    using var document = JsonDocument.Parse(json.ToString());
                    return await AuthorityCallAsync(
                            database,
                            "commitAggregate",
                            string.Empty,
                            new
                            {
                                aggregateKey =
                                    $"{fence.AggregateId:D}:"
                                    + fence.AggregateGeneration
                            },
                            recordKey: aggregateKey,
                            extraKeys:
                                await BuildAggregateExtraKeysAsync(
                                        database,
                                        document.RootElement,
                                        commit: true,
                                        abort: false)
                                    .ConfigureAwait(false))
                        .ConfigureAwait(false);
                },
                cancellationToken)
            .ConfigureAwait(false);
        if (result.ValueKind == JsonValueKind.Undefined)
            return ZLinkAggregateCommitResult.Stale;
        return result.GetProperty("kind").GetString() switch
        {
            "committed" => ZLinkAggregateCommitResult.Committed,
            "alreadyCommitted" =>
                ZLinkAggregateCommitResult.AlreadyCommitted,
            "stale" => ZLinkAggregateCommitResult.Stale,
            "generationExhausted" =>
                ZLinkAggregateCommitResult.GenerationExhausted,
            var status => throw new InvalidOperationException(
                $"Unknown Redis aggregate commit result '{status}'.")
        };
    }

    public async ValueTask<ZLinkAggregateAbortResult> AbortAggregateAsync(
        ZLinkAggregateFence fence,
        CancellationToken cancellationToken = default)
    {
        ValidateFence(fence);
        var aggregateKey = _keys.HybridAggregateKey(
            fence.AggregateId,
            fence.AggregateGeneration);
        var result = await ExecuteAsync(
                async database =>
                {
                    var json = await database.HashGetAsync(
                            aggregateKey,
                            "requestJson")
                        .ConfigureAwait(false);
                    if (json.IsNull)
                        return default;
                    using var document = JsonDocument.Parse(json.ToString());
                    return await AuthorityCallAsync(
                            database,
                            "abortAggregate",
                            string.Empty,
                            new
                            {
                                aggregateKey =
                                    $"{fence.AggregateId:D}:"
                                    + fence.AggregateGeneration
                            },
                            recordKey: aggregateKey,
                            extraKeys:
                                await BuildAggregateExtraKeysAsync(
                                        database,
                                        document.RootElement,
                                        commit: false,
                                        abort: true)
                                    .ConfigureAwait(false))
                        .ConfigureAwait(false);
                },
                cancellationToken)
            .ConfigureAwait(false);
        if (result.ValueKind == JsonValueKind.Undefined)
            return ZLinkAggregateAbortResult.Stale;
        return result.GetProperty("kind").GetString() switch
        {
            "aborted" => ZLinkAggregateAbortResult.Aborted,
            "alreadyAborted" => ZLinkAggregateAbortResult.AlreadyAborted,
            "stale" => ZLinkAggregateAbortResult.Stale,
            var status => throw new InvalidOperationException(
                $"Unknown Redis aggregate abort result '{status}'.")
        };
    }

    private RedisKey HybridDescriptorRowKey(
        ZLinkMeshNodeDescriptorKey descriptor) =>
        _keys.HybridDescriptorKey(
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(descriptor));

    private async ValueTask<JsonElement> AuthorityCallAsync(
        IDatabase database,
        string operation,
        string authorityKey,
        object request,
        RedisKey? descriptorKey = null,
        RedisKey? descriptorAdmissionKey = null,
        RedisKey? targetLeaseKey = null,
        RedisKey? recordKey = null,
        RedisKey? currentLeaseKey = null,
        IReadOnlyList<RedisKey>? extraKeys = null)
    {
        var placeholder = _keys.HybridSchemaKey();
        var scriptKeys = new List<RedisKey>
        {
            string.IsNullOrEmpty(authorityKey)
                ? placeholder
                : _keys.HybridAuthorityCurrentKey(authorityKey),
            string.IsNullOrEmpty(authorityKey)
                ? placeholder
                : _keys.HybridAuthorityHistoryKey(authorityKey),
            string.IsNullOrEmpty(authorityKey)
                ? placeholder
                : _keys.HybridAuthorityHistoryRevisionsKey(authorityKey),
            _keys.HybridCounterKey(),
            _keys.HybridAuthorityKeyIndexKey(),
            _keys.HybridMembershipCurrentKey(),
            _keys.HybridCapacityKey(type: false, pending: false),
            _keys.HybridCapacityKey(type: false, pending: true),
            _keys.HybridCapacityKey(type: true, pending: false),
            _keys.HybridCapacityKey(type: true, pending: true),
            descriptorKey ?? placeholder,
            descriptorAdmissionKey ?? placeholder,
            targetLeaseKey ?? placeholder,
            recordKey ?? placeholder,
            currentLeaseKey ?? placeholder,
            _keys.HybridAuthorityIndexGcKey(),
            _keys.HybridScanExpiryKey(),
            _keys.HybridScanWatermarkKey(),
            placeholder
        };
        if (extraKeys is not null)
            scriptKeys.AddRange(extraKeys);
        var requestNode = JsonSerializer.SerializeToNode(request)
            ?.AsObject()
            ?? throw new InvalidOperationException(
                "Redis authority request must serialize as an object.");
        if (!string.IsNullOrEmpty(authorityKey))
            requestNode["keyHex"] =
                ZLinkRedisLocationKeys.CanonicalKeyHex(authorityKey);
        var requestJson = requestNode.ToJsonString();
        if (Encoding.UTF8.GetByteCount(requestJson) > 1024 * 1024)
            throw new ArgumentOutOfRangeException(
                nameof(request),
                "Redis authority request exceeds 1 MiB.");
        var raw = (string)(await database.ScriptEvaluateAsync(
            ZLinkRedisAuthorityScripts.Unified,
            [.. scriptKeys],
            [operation, requestJson]).ConfigureAwait(false))!;
        using var document = JsonDocument.Parse(raw);
        return document.RootElement.Clone();
    }

    private async ValueTask<RedisKey[]> BuildAggregateExtraKeysAsync(
        IDatabase database,
        JsonElement request,
        bool commit,
        bool abort)
    {
        var participants = request.GetProperty("participants")
            .EnumerateArray()
            .ToArray();
        var keys = new List<RedisKey>();
        foreach (var participant in participants)
            keys.Add(_keys.HybridAuthorityCurrentKey(
                participant.GetProperty("key").GetString()!));
        if (commit)
        {
            foreach (var participant in participants)
                keys.Add(_keys.HybridAuthorityHistoryKey(
                    participant.GetProperty("key").GetString()!));
            foreach (var participant in participants)
                keys.Add(_keys.HybridAuthorityHistoryRevisionsKey(
                    participant.GetProperty("key").GetString()!));
            foreach (var participant in participants)
                keys.Add(_keys.HybridMembershipHistoryKey(
                    participant.GetProperty("key").GetString()!));
            foreach (var participant in participants)
                keys.Add(_keys.HybridMembershipHistoryRevisionsKey(
                    participant.GetProperty("key").GetString()!));
        }
        if (abort)
            return [.. keys];
        foreach (var participant in participants)
        {
            var current = await database.HashGetAsync(
                    _keys.HybridAuthorityCurrentKey(
                        participant.GetProperty("key").GetString()!),
                    "ownerId")
                .ConfigureAwait(false);
            keys.Add(current.IsNull
                ? _keys.HybridSchemaKey()
                : _keys.HybridOwnerLeaseKey(current.ToString()));
        }
        var target = request.GetProperty("target");
        var targetDescriptorKey =
            target.GetProperty("descriptorKey").GetString()!;
        keys.Add(_keys.HybridDescriptorKey(targetDescriptorKey));
        keys.Add(_keys.HybridDescriptorAdmissionKey(targetDescriptorKey));
        keys.Add(_keys.HybridOwnerLeaseKey(
            target.GetProperty("owner").GetProperty("ownerId").GetString()!));
        return [.. keys];
    }

    private static ZLinkAuthoritySnapshot Snapshot(
        JsonElement value,
        DateTimeOffset storeNow)
    {
        var allocation = value.GetProperty("allocation");
        var descriptor = allocation.GetProperty("descriptor");
        ZLinkReservedObjectCreation? pendingCreation = null;
        if (value.TryGetProperty("pendingCreation", out var pending)
            && pending.ValueKind == JsonValueKind.Object)
        {
            pendingCreation = new ZLinkReservedObjectCreation(
                pending.GetProperty("reservationId").GetString()!,
                pending.GetProperty("requestContentReference").GetString()!,
                Convert.FromBase64String(
                    pending.GetProperty("requestSha256").GetString()!),
                pending.GetProperty("requestEncodedSize").GetInt32());
        }
        return new ZLinkAuthoritySnapshot(
            value.GetProperty("storeVersion").GetString()!,
            Convert.FromBase64String(value.GetProperty("payload").GetString()!),
            ulong.Parse(
                value.GetProperty("objectGeneration").GetString()!,
                System.Globalization.CultureInfo.InvariantCulture),
            ulong.Parse(
                value.GetProperty("authorityOwnerGeneration").GetString()!,
                System.Globalization.CultureInfo.InvariantCulture),
            value.GetProperty("ownerId").GetString()!,
            long.Parse(
                value.GetProperty("ownerLeaseGeneration").GetString()!,
                System.Globalization.CultureInfo.InvariantCulture),
            new ZLinkPlacementAllocation(
                allocation.GetProperty("state").GetString() switch
                {
                    "pending" => ZLinkPlacementAllocationState.Reserved,
                    "active" => ZLinkPlacementAllocationState.Active,
                    var state => throw new InvalidDataException(
                        $"Redis authority allocation state '{state}' is invalid.")
                },
                allocation.GetProperty("objectKind").GetString() switch
                {
                    "actor" => ZLinkPlacementObjectKind.Actor,
                    "user_spot" => ZLinkPlacementObjectKind.UserSpot,
                    "instance_spot" => ZLinkPlacementObjectKind.InstanceSpot,
                    var kind => throw new InvalidDataException(
                        $"Redis authority object kind '{kind}' is invalid.")
                },
                allocation.GetProperty("stableType").GetString()!,
                new ZLinkMeshNodeDescriptorKey(
                    descriptor.GetProperty("meshName").GetString()!,
                    RoutingId.FromHex(
                        descriptor.GetProperty("rid").GetString()!)),
                ulong.Parse(
                    allocation.GetProperty(
                            "descriptorLifecycleGeneration")
                        .GetString()!,
                    System.Globalization.CultureInfo.InvariantCulture),
                DecodeCapacityBundle(
                    allocation.GetProperty("capacityBundle").GetString()!)),
            pendingCreation,
            storeNow);
    }

    private static ZLinkAuthorityReadResult AuthorityRead(JsonElement value)
    {
        var now = DateTimeOffset.FromUnixTimeMilliseconds(
            value.GetProperty("storeNowMs").GetInt64());
        return value.GetProperty("kind").GetString() == "missing"
            ? new ZLinkAuthorityReadResult.Missing(now)
            : new ZLinkAuthorityReadResult.Found(Snapshot(value, now));
    }

    private static object OwnerJson(ZLinkLocationOwnerToken owner) => new
    {
        ownerId = owner.OwnerId,
        leaseGeneration = owner.LeaseGeneration.ToString(
            System.Globalization.CultureInfo.InvariantCulture)
    };

    private static object CapacityJson(ZLinkCapacityVector capacity) => new
    {
        actors = capacity.Actors,
        spots = capacity.Spots,
        spotType = capacity.SpotType is null
            ? null
            : new
            {
                objectKind = ObjectKindToken(capacity.SpotType.ObjectKind),
                stableType = capacity.SpotType.StableType,
                count = capacity.SpotType.Count
            }
    };

    private static string EncodeCapacityBundle(ZLinkCapacityVector capacity)
    {
        static string Segment(string value) =>
            $"{Encoding.UTF8.GetByteCount(value)}:{value}";
        var encoded = Segment("zlink-capacity-bundle-v2")
                      + Segment(capacity.Actors.ToString(
                          System.Globalization.CultureInfo.InvariantCulture))
                      + Segment(capacity.Spots.ToString(
                          System.Globalization.CultureInfo.InvariantCulture));
        if (capacity.SpotType is not { } spotType)
            return encoded + Segment("0");
        return encoded
               + Segment("1")
               + Segment(ObjectKindToken(spotType.ObjectKind))
               + Segment(spotType.StableType)
               + Segment(spotType.Count.ToString(
                   System.Globalization.CultureInfo.InvariantCulture));
    }

    private static ZLinkCapacityVector DecodeCapacityBundle(string encoded)
    {
        var bytes = Encoding.UTF8.GetBytes(encoded);
        var offset = 0;
        string ReadSegment()
        {
            var colon = Array.IndexOf(bytes, (byte)':', offset);
            if (colon < offset
                || !int.TryParse(
                    Encoding.ASCII.GetString(bytes, offset, colon - offset),
                    out var length)
                || length < 0
                || colon + 1 + length > bytes.Length)
                throw new InvalidDataException(
                    "Redis capacity bundle is invalid.");
            var value = Encoding.UTF8.GetString(bytes, colon + 1, length);
            offset = colon + 1 + length;
            return value;
        }

        if (ReadSegment() != "zlink-capacity-bundle-v2"
            || !int.TryParse(ReadSegment(), out var actors)
            || !int.TryParse(ReadSegment(), out var spots))
            throw new InvalidDataException("Redis capacity bundle is invalid.");
        var presence = ReadSegment();
        ZLinkSpotTypeCapacityDelta? spotType = null;
        if (presence == "1")
        {
            var kind = ReadSegment() switch
            {
                "user_spot" => ZLinkPlacementObjectKind.UserSpot,
                "instance_spot" => ZLinkPlacementObjectKind.InstanceSpot,
                _ => throw new InvalidDataException(
                    "Redis capacity bundle Spot kind is invalid.")
            };
            var stableType = ReadSegment();
            if (!int.TryParse(ReadSegment(), out var count))
                throw new InvalidDataException(
                    "Redis capacity bundle Spot count is invalid.");
            spotType = new ZLinkSpotTypeCapacityDelta(
                kind,
                stableType,
                count);
        }
        else if (presence != "0")
            throw new InvalidDataException("Redis capacity bundle is invalid.");
        if (offset != bytes.Length)
            throw new InvalidDataException("Redis capacity bundle is invalid.");
        return new ZLinkCapacityVector(actors, spots, spotType);
    }

    private static string ObjectKindToken(ZLinkPlacementObjectKind kind) =>
        kind switch
        {
            ZLinkPlacementObjectKind.Actor => "actor",
            ZLinkPlacementObjectKind.UserSpot => "user_spot",
            ZLinkPlacementObjectKind.InstanceSpot => "instance_spot",
            _ => throw new ArgumentOutOfRangeException(nameof(kind))
        };

    private static void ValidateCreationTerminal(
        ZLinkCreationTerminalPublication publication)
    {
        ArgumentNullException.ThrowIfNull(publication);
        ValidateCreationOperation(publication.Operation);
        if (publication.TerminalEnvelope.Length > 1024 * 1024
            || publication.TerminalEnvelopeSha256.Length != 32)
            throw new ArgumentOutOfRangeException(nameof(publication));
        Span<byte> digest = stackalloc byte[32];
        SHA256.HashData(publication.TerminalEnvelope.Span, digest);
        if (!CryptographicOperations.FixedTimeEquals(
                digest,
                publication.TerminalEnvelopeSha256.Span))
            throw new ArgumentException(
                "The creation terminal SHA-256 does not match its envelope.",
                nameof(publication));
    }

    private static void ValidateCreationOperation(
        ZLinkCreationOperationId operation)
    {
        if (operation.SourceNodeRid.IsEmpty
            || operation.SourceNodeGeneration == 0
            || (operation.OperationIdHigh == 0 && operation.OperationIdLow == 0))
            throw new ArgumentOutOfRangeException(nameof(operation));
    }

    private static ZLinkCreationTerminalRecord CreationTerminal(
        ZLinkCreationOperationId operation,
        JsonElement value,
        DateTimeOffset storeNow) =>
        new(
            operation,
            value.GetProperty("reservationId").GetString()!,
            value.GetProperty("objectKind").GetString() switch
            {
                "actor" => ZLinkPlacementObjectKind.Actor,
                "user_spot" => ZLinkPlacementObjectKind.UserSpot,
                "instance_spot" => ZLinkPlacementObjectKind.InstanceSpot,
                var kind => throw new InvalidDataException(
                    $"Redis creation terminal object kind '{kind}' is invalid.")
            },
            value.GetProperty("state").GetString() switch
            {
                "Created" => ZLinkCreationTerminalState.Created,
                "Rejected" => ZLinkCreationTerminalState.Rejected,
                "Failed" => ZLinkCreationTerminalState.Failed,
                var state => throw new InvalidDataException(
                    $"Redis creation terminal state '{state}' is invalid.")
            },
            Convert.FromHexString(value.GetProperty("terminalEnvelope").GetString()!),
            Convert.FromHexString(
                value.GetProperty("terminalEnvelopeSha256").GetString()!),
            DateTimeOffset.FromUnixTimeMilliseconds(
                long.Parse(
                    value.GetProperty("expiresAtUnixMs").GetString()!,
                    System.Globalization.CultureInfo.InvariantCulture)),
            storeNow);

    private static (string ScanId, string LastHex) ParseCursor(
        ZLinkAuthorityScanCursor cursor)
    {
        var separator = cursor.Encoded.LastIndexOf(':');
        if (separator <= 0
            || !Guid.TryParseExact(
                cursor.Encoded[..separator],
                "N",
                out _))
            throw new ArgumentException(
                "The Redis authority scan cursor is invalid.",
                nameof(cursor));
        var lastHex = cursor.Encoded[(separator + 1)..];
        if (lastHex.Length == 0
            || lastHex.Length % 2 != 0
            || lastHex.Any(static value =>
                !Uri.IsHexDigit(value)
                || char.IsUpper(value)))
            throw new ArgumentException(
                "The Redis authority scan cursor is invalid.",
                nameof(cursor));
        return (cursor.Encoded[..separator], lastHex);
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
            || !IsAllocationCapacityValid(
                request.ObjectKind,
                request.StableType,
                request.Capacity)
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
            || !IsAllocationCapacityValid(
                request.ObjectKind,
                request.StableType,
                request.Capacity))
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
            || request.TargetDescriptorLifecycleGeneration == 0
            || !IsAggregateCapacityValid(request)
            || request.TargetOwner.LeaseGeneration <= 0)
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

    private static bool IsAggregateCapacityValid(
        ZLinkAggregatePrepareRequest request)
    {
        var preservesOwner = request.Participants.All(static participant =>
            participant.OwnerTransition
            == ZLinkAuthorityGenerationTransition.Preserve);
        var hasNoCapacityDelta = request.Capacity.Actors == 0
                                 && request.Capacity.Spots == 0
                                 && request.Capacity.SpotType is null;
        return preservesOwner
            ? hasNoCapacityDelta
              && request.Participants.All(static participant =>
                  participant.MembershipMutation.IsEmpty)
            : !hasNoCapacityDelta && IsCapacityVectorValid(request.Capacity);
    }

    private static bool IsCapacityVectorValid(ZLinkCapacityVector capacity)
    {
        ArgumentNullException.ThrowIfNull(capacity);
        if (capacity.Actors < 0
            || capacity.Spots < 0
            || capacity.Actors == 0 && capacity.Spots == 0)
            return false;
        if (capacity.SpotType is not { } spotType)
            return capacity.Spots == 0;
        return capacity.Spots == spotType.Count
               && spotType.Count > 0
               && spotType.ObjectKind is ZLinkPlacementObjectKind.UserSpot
                   or ZLinkPlacementObjectKind.InstanceSpot
               && !string.IsNullOrWhiteSpace(spotType.StableType);
    }

    private static bool IsAllocationCapacityValid(
        ZLinkPlacementObjectKind objectKind,
        string stableType,
        ZLinkCapacityVector capacity)
    {
        if (!IsCapacityVectorValid(capacity))
            return false;
        return objectKind switch
        {
            ZLinkPlacementObjectKind.Actor =>
                capacity.Actors == 1
                && capacity.Spots == 0
                && capacity.SpotType is null,
            ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot =>
                capacity.Actors == 0
                && capacity.Spots == 1
                && capacity.SpotType is { Count: 1 } spotType
                && spotType.ObjectKind == objectKind
                && string.Equals(
                    spotType.StableType,
                    stableType,
                    StringComparison.Ordinal),
            _ => false
        };
    }

    private static void ValidateFence(ZLinkAggregateFence fence)
    {
        if (fence.AggregateId == Guid.Empty
            || fence.AggregateGeneration is 0 or > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(fence));
    }
}
