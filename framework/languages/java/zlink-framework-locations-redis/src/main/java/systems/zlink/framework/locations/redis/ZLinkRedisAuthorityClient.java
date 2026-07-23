package systems.zlink.framework.locations.redis;

import io.lettuce.core.ScriptOutputType;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.*;

final class ZLinkRedisAuthorityClient {
    private static final String PROLOGUE = """
        if redis.replicate_commands then redis.replicate_commands() end
        local time = redis.call('TIME')
        local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
        """;
    private static final String READ = PROLOGUE + """
        if redis.call('EXISTS', KEYS[1]) == 0 then
            return {'missing', nowMs}
        end
        return {'found', nowMs,
            redis.call('HGET', KEYS[1], 'version'),
            redis.call('HGET', KEYS[1], 'payload'),
            redis.call('HGET', KEYS[1], 'objectGeneration'),
            redis.call('HGET', KEYS[1], 'ownerGeneration'),
            redis.call('HGET', KEYS[1], 'ownerId'),
            redis.call('HGET', KEYS[1], 'ownerLeaseGeneration'),
            redis.call('HGET', KEYS[1], 'allocationState'),
            redis.call('HGET', KEYS[1], 'allocationObjectKind'),
            redis.call('HGET', KEYS[1], 'stableType'),
            redis.call('HGET', KEYS[1], 'targetDescriptor'),
            redis.call('HGET', KEYS[1], 'targetDescriptorLifecycleGeneration'),
            redis.call('HGET', KEYS[1], 'allocationCapacityDelta')}
        """;
    private static final String CAS = PROLOGUE + """
        local exists = redis.call('EXISTS', KEYS[1]) == 1
        if not exists
            or redis.call('HGET', KEYS[1], 'version') ~= ARGV[2]
            or redis.call('HGET', KEYS[1], 'allocationState') ~= 'active' then
            return {'conflict', nowMs}
        end
        local function leaseIsLive(ownerId, generation)
            local lease = redis.call('HGET', KEYS[4], ownerId)
            if not lease then return false end
            local leaseGeneration, expiresAt =
                string.match(lease, '([^|]*)|([^|]*)')
            return leaseGeneration == generation
                and tonumber(expiresAt) > nowMs
        end
        if ARGV[3] == 'delete' then
            local currentOwner = redis.call('HGET', KEYS[1], 'ownerId')
            local currentLease =
                redis.call('HGET', KEYS[1], 'ownerLeaseGeneration')
            if not leaseIsLive(currentOwner, currentLease) then
                return {'conflict', nowMs}
            end
            if redis.call('GET', KEYS[3]) == '9223372036854775807' then
                return {'generation-exhausted', nowMs}
            end
            local revision = redis.call('INCR', KEYS[3])
            local capacityField =
                redis.call('HGET', KEYS[1], 'targetDescriptor') .. '|' ..
                redis.call('HGET', KEYS[1], 'targetDescriptorLifecycleGeneration')
            redis.call('HINCRBY', KEYS[8], capacityField .. ':active',
                -tonumber(redis.call('HGET', KEYS[1], 'allocationCapacityDelta')))
            redis.call('DEL', KEYS[1])
            redis.call('ZREM', KEYS[2], ARGV[6])
            return {'deleted', nowMs, revision}
        end
        local targetOwner = ARGV[7]
        local targetLease = ARGV[8]
        if ARGV[4] == 'preserve' then
            targetOwner = redis.call('HGET', KEYS[1], 'ownerId')
            targetLease =
                redis.call('HGET', KEYS[1], 'ownerLeaseGeneration')
        end
        if not leaseIsLive(targetOwner, targetLease) then
            return {'conflict', nowMs}
        end
        if ARGV[4] == 'new-owner' then
            local capacity = redis.call('HGET', KEYS[7], ARGV[9])
            local currentVersion =
                redis.call('HGET', KEYS[1], 'version')
            local currentOwner =
                redis.call('HGET', KEYS[1], 'ownerId')
            local currentLease =
                redis.call('HGET', KEYS[1], 'ownerLeaseGeneration')
            if not capacity
                or capacity ~= 'prepared|' .. ARGV[10] .. '|'
                    .. currentVersion .. '|'
                    .. currentOwner .. '|' .. currentLease .. '|'
                    .. targetOwner .. '|' .. targetLease then
                return {'conflict', nowMs}
            end
        end
        if redis.call('GET', KEYS[3]) == '9223372036854775807'
            or (ARGV[4] ~= 'preserve'
                and redis.call('GET', KEYS[6]) == '9223372036854775807') then
            return {'generation-exhausted', nowMs}
        end
        local revision = redis.call('INCR', KEYS[3])
        local objectGeneration =
            redis.call('HGET', KEYS[1], 'objectGeneration')
        local ownerGeneration = ARGV[4] == 'preserve'
            and redis.call('HGET', KEYS[1], 'ownerGeneration')
            or redis.call('INCR', KEYS[6])
        redis.call('HSET', KEYS[1],
            'version', revision,
            'payload', ARGV[5],
            'objectGeneration', objectGeneration,
            'ownerGeneration', ownerGeneration,
            'ownerId', targetOwner,
            'ownerLeaseGeneration', targetLease,
            'authorityKey', ARGV[10])
        redis.call('ZADD', KEYS[2], 0, ARGV[6])
        if ARGV[4] == 'new-owner' then
            redis.call('HSET', KEYS[7], ARGV[9],
                'committed|' .. ARGV[6])
        end
        return {'stored', nowMs, revision,
            objectGeneration, ownerGeneration, targetOwner, targetLease,
            redis.call('HGET', KEYS[1], 'allocationState'),
            redis.call('HGET', KEYS[1], 'allocationObjectKind'),
            redis.call('HGET', KEYS[1], 'stableType'),
            redis.call('HGET', KEYS[1], 'targetDescriptor'),
            redis.call('HGET', KEYS[1], 'targetDescriptorLifecycleGeneration'),
            redis.call('HGET', KEYS[1], 'allocationCapacityDelta')}
        """;
    private static final String RESERVE = PROLOGUE + """
        local lease = redis.call('HGET', KEYS[6], ARGV[3])
        if not lease then return {'owner-stale', nowMs} end
        local leaseGeneration, leaseExpiry =
            string.match(lease, '([^|]*)|([^|]*)')
        if leaseGeneration ~= ARGV[4]
            or tonumber(leaseExpiry) <= nowMs then
            return {'owner-stale', nowMs}
        end
        if redis.call('EXISTS', KEYS[1]) == 1 then
            local status = redis.call('HGET', KEYS[1], 'stableType') == ARGV[2]
                and 'already-exists' or 'type-mismatch'
            return {status, nowMs,
                redis.call('HGET', KEYS[1], 'version'),
                redis.call('HGET', KEYS[1], 'payload'),
                redis.call('HGET', KEYS[1], 'objectGeneration'),
                redis.call('HGET', KEYS[1], 'ownerGeneration'),
                redis.call('HGET', KEYS[1], 'ownerId'),
                redis.call('HGET', KEYS[1], 'ownerLeaseGeneration'),
                redis.call('HGET', KEYS[1], 'allocationState'),
                redis.call('HGET', KEYS[1], 'allocationObjectKind'),
                redis.call('HGET', KEYS[1], 'stableType'),
                redis.call('HGET', KEYS[1], 'targetDescriptor'),
                redis.call('HGET', KEYS[1], 'targetDescriptorLifecycleGeneration'),
                redis.call('HGET', KEYS[1], 'allocationCapacityDelta')}
        end
        if redis.call('GET', KEYS[4]) == '9223372036854775807'
            or redis.call('GET', KEYS[5]) == '9223372036854775807'
            or redis.call('GET', KEYS[3]) == '9223372036854775807' then
            return {'generation-exhausted', nowMs}
        end
        local objectGeneration = redis.call('INCR', KEYS[4])
        local ownerGeneration = redis.call('INCR', KEYS[5])
        local revision = redis.call('INCR', KEYS[3])
        local reservationVersion = tostring(revision)
        redis.call('HSET', KEYS[1],
            'version', revision,
            'payload', ARGV[8],
            'objectGeneration', objectGeneration,
            'ownerGeneration', ownerGeneration,
            'ownerId', ARGV[3],
            'ownerLeaseGeneration', ARGV[4],
            'stableType', ARGV[2],
            'authorityKey', ARGV[7],
            'reservationVersion', reservationVersion,
            'creationIntentReference', ARGV[5],
            'creationIntentHash', ARGV[6],
            'targetDescriptor', ARGV[9],
            'targetDescriptorLifecycleGeneration', ARGV[10],
            'allocationState', 'pending',
            'allocationObjectKind', ARGV[11],
            'allocationCapacityDelta', ARGV[12])
        local capacityField = ARGV[9] .. '|' .. ARGV[10]
        redis.call('HINCRBY', KEYS[7], capacityField .. ':pending',
            tonumber(ARGV[12]))
        redis.call('ZADD', KEYS[2], 0, ARGV[1])
        return {'reserved', nowMs, revision, objectGeneration,
            ownerGeneration, reservationVersion}
        """;
    private static final String COMMIT_RESERVATION = PROLOGUE + """
        if redis.call('EXISTS', KEYS[1]) == 0 then
            return {'stale', nowMs}
        end
        local currentReservation = redis.call('HGET', KEYS[1], 'reservationVersion')
        if not currentReservation then
            if redis.call('HGET', KEYS[1], 'allocationState') == 'active'
                and redis.call('HGET', KEYS[1], 'objectGeneration') == ARGV[2]
                and redis.call('HGET', KEYS[1], 'ownerGeneration') == ARGV[3]
                and redis.call('HGET', KEYS[1], 'ownerId') == ARGV[4]
                and redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') == ARGV[5]
                and redis.call('HGET', KEYS[1], 'targetDescriptor') == ARGV[6]
                and redis.call('HGET', KEYS[1], 'targetDescriptorLifecycleGeneration') == ARGV[7] then
                return {'already-committed', nowMs}
            end
            return {'stale', nowMs}
        end
        if currentReservation ~= ARGV[1]
            or redis.call('HGET', KEYS[1], 'objectGeneration') ~= ARGV[2]
            or redis.call('HGET', KEYS[1], 'ownerGeneration') ~= ARGV[3]
            or redis.call('HGET', KEYS[1], 'ownerId') ~= ARGV[4]
            or redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') ~= ARGV[5]
            or redis.call('HGET', KEYS[1], 'targetDescriptor') ~= ARGV[6]
            or redis.call('HGET', KEYS[1], 'targetDescriptorLifecycleGeneration') ~= ARGV[7] then
            return {'stale', nowMs}
        end
        if redis.call('HGET', KEYS[1], 'allocationState') ~= 'pending' then
            return {'stale', nowMs}
        end
        local lease = redis.call('HGET', KEYS[3], ARGV[4])
        if not lease then return {'stale', nowMs} end
        local leaseGeneration, leaseExpiry =
            string.match(lease, '([^|]*)|([^|]*)')
        if leaseGeneration ~= ARGV[5]
            or tonumber(leaseExpiry) <= nowMs then
            return {'stale', nowMs}
        end
        if redis.call('GET', KEYS[2]) == '9223372036854775807' then
            return {'generation-exhausted', nowMs}
        end
        local revision = redis.call('INCR', KEYS[2])
        local capacityField = ARGV[6] .. '|' .. ARGV[7]
        local capacityDelta =
            tonumber(redis.call('HGET', KEYS[1], 'allocationCapacityDelta'))
        redis.call('HINCRBY', KEYS[4], capacityField .. ':pending',
            -capacityDelta)
        redis.call('HINCRBY', KEYS[4], capacityField .. ':active',
            capacityDelta)
        redis.call('HSET', KEYS[1],
            'version', revision,
            'payload', ARGV[8],
            'allocationState', 'active')
        redis.call('HDEL', KEYS[1],
            'reservationVersion', 'creationIntentReference', 'creationIntentHash')
        return {'committed', nowMs}
        """;
    private static final String ABORT_RESERVATION = PROLOGUE + """
        if redis.call('EXISTS', KEYS[1]) == 0 then
            return {'already-aborted', nowMs}
        end
        if redis.call('HGET', KEYS[1], 'reservationVersion') ~= ARGV[1]
            or redis.call('HGET', KEYS[1], 'objectGeneration') ~= ARGV[2]
            or redis.call('HGET', KEYS[1], 'ownerGeneration') ~= ARGV[3]
            or redis.call('HGET', KEYS[1], 'ownerId') ~= ARGV[4]
            or redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') ~= ARGV[5]
            or redis.call('HGET', KEYS[1], 'targetDescriptor') ~= ARGV[6]
            or redis.call('HGET', KEYS[1], 'targetDescriptorLifecycleGeneration') ~= ARGV[7] then
            return {'stale', nowMs}
        end
        if redis.call('HGET', KEYS[1], 'allocationState') ~= 'pending' then
            return {'stale', nowMs}
        end
        local capacityField = ARGV[6] .. '|' .. ARGV[7]
        redis.call('HINCRBY', KEYS[5], capacityField .. ':pending',
            -tonumber(redis.call('HGET', KEYS[1], 'allocationCapacityDelta')))
        redis.call('DEL', KEYS[1])
        redis.call('ZREM', KEYS[2], ARGV[8])
        redis.call('INCR', KEYS[3])
        return {'aborted', nowMs}
        """;
    private static final String RESERVE_RELOCATION_CAPACITY = PROLOGUE + """
        if redis.call('EXISTS', KEYS[1]) == 0
            or redis.call('HGET', KEYS[1], 'version') ~= ARGV[1] then
            return {'conflict', nowMs}
        end
        local function leaseIsLive(ownerId, generation)
            local lease = redis.call('HGET', KEYS[2], ownerId)
            if not lease then return false end
            local leaseGeneration, expiresAt =
                string.match(lease, '([^|]*)|([^|]*)')
            return leaseGeneration == generation
                and tonumber(expiresAt) > nowMs
        end
        if redis.call('HGET', KEYS[1], 'ownerId') ~= ARGV[2]
            or redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') ~= ARGV[3]
            or redis.call('HGET', KEYS[1], 'allocationState') ~= 'active'
            or redis.call('HGET', KEYS[1], 'allocationObjectKind') ~= ARGV[6]
            or redis.call('HGET', KEYS[1], 'stableType') ~= ARGV[7]
            or redis.call('HGET', KEYS[1], 'targetDescriptor') ~= ARGV[8]
            or redis.call('HGET', KEYS[1], 'targetDescriptorLifecycleGeneration') ~= ARGV[9]
            or redis.call('HGET', KEYS[1], 'allocationCapacityDelta') ~= ARGV[10] then
            return {'conflict', nowMs}
        end
        if not leaseIsLive(ARGV[4], ARGV[5]) then
            return {'target-unavailable', nowMs}
        end
        return {'target-unavailable', nowMs}
        """;
    private static final String LIST = PROLOGUE + """
        local revision = tostring(redis.call('GET', KEYS[2]) or '0')
        if ARGV[1] ~= '' and ARGV[1] ~= revision then
            return {'expired', nowMs}
        end
        local offset = tonumber(ARGV[2])
        local encodedKeys = redis.call('ZRANGE', KEYS[1], offset, offset + tonumber(ARGV[3]) - 1)
        local out = {}
        for _, encodedKey in ipairs(encodedKeys) do
            local row = ARGV[4] .. encodedKey
            local key = redis.call('HGET', row, 'authorityKey')
            if key and string.sub(key, 1, string.len(ARGV[5])) == ARGV[5] then
                out[#out + 1] = key
                out[#out + 1] = redis.call('HGET', row, 'version')
                out[#out + 1] = redis.call('HGET', row, 'payload')
                out[#out + 1] = redis.call('HGET', row, 'objectGeneration')
                out[#out + 1] = redis.call('HGET', row, 'ownerGeneration')
                out[#out + 1] = redis.call('HGET', row, 'ownerId')
                out[#out + 1] = redis.call('HGET', row, 'ownerLeaseGeneration')
                out[#out + 1] = redis.call('HGET', row, 'allocationState')
                out[#out + 1] = redis.call('HGET', row, 'allocationObjectKind')
                out[#out + 1] = redis.call('HGET', row, 'stableType')
                out[#out + 1] = redis.call('HGET', row, 'targetDescriptor')
                out[#out + 1] = redis.call('HGET', row, 'targetDescriptorLifecycleGeneration')
                out[#out + 1] = redis.call('HGET', row, 'allocationCapacityDelta')
            end
        end
        local nextOffset = offset + #encodedKeys
        local total = redis.call('ZCARD', KEYS[1])
        return {'page', nowMs, revision, nextOffset < total and nextOffset or -1, out}
        """;

    private final ZLinkRedisLocationConnection connection;
    private final ZLinkRedisLocationKeys keys;

    ZLinkRedisAuthorityClient(
        ZLinkRedisLocationConnection connection,
        ZLinkRedisLocationKeys keys) {
        this.connection = connection;
        this.keys = keys;
    }

    CompletionStage<ZLinkAuthorityReadResult> read(
        String key,
        ZLinkStoreCancellation cancellation) {
        requireKey(key);
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                READ,
                ScriptOutputType.MULTI,
                new String[] {keys.authorityRowKey(key)}))
            .thenApply(this::readResult);
    }

    CompletionStage<ZLinkAuthorityWriteResult> compareExchange(
        String key,
        ZLinkAuthorityExpectation expectation,
        ZLinkAuthorityMutation mutation,
        ZLinkStoreCancellation cancellation) {
        requireKey(key);
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        String expectationKind = "found";
        String version =
            ((ZLinkAuthorityExpectFound) expectation).storeVersion();
        boolean deleting = mutation instanceof ZLinkAuthorityDelete;
        String payload = deleting
            ? ""
            : encode(((ZLinkAuthorityPut) mutation).payload());
        ZLinkAuthorityPut put = deleting
            ? null
            : (ZLinkAuthorityPut) mutation;
        String transition = deleting
            ? "preserve"
            : switch (put.generationTransition()) {
                case PRESERVE -> "preserve";
                case NEW_OWNER -> "new-owner";
            };
        ZLinkLocationOwnerToken targetOwner = put == null
            ? null
            : put.targetOwner().orElse(null);
        String capacityFence = put == null
            ? ""
            : put.relocationCapacityFence()
                .map(ZLinkRelocationCapacityFence::value)
                .orElse("");
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                CAS,
                ScriptOutputType.MULTI,
                new String[] {
                    keys.authorityRowKey(key),
                    keys.authorityIndexKey(),
                    keys.authorityRevisionKey(),
                    keys.leaseStateKey(),
                    keys.authorityObjectGenerationKey(),
                    keys.authorityOwnerGenerationKey(),
                    keys.relocationCapacityStateKey(),
                    keys.placementCapacityStateKey()
                },
                expectationKind,
                version,
                deleting ? "delete" : "put",
                transition,
                payload,
                keys.encodedAuthorityKey(key),
                targetOwner == null ? "" : targetOwner.ownerId(),
                targetOwner == null
                    ? ""
                    : Long.toString(targetOwner.leaseGeneration()),
                capacityFence,
                key))
            .thenCompose(raw -> {
                String status = string(raw.getFirst());
                if ("conflict".equals(status)) {
                    return read(key, cancellation)
                        .thenApply(ZLinkAuthorityConflict::new);
                }
                Instant now = time(raw, 1);
                if ("deleted".equals(status)) {
                    return CompletableFuture.completedFuture(
                        new ZLinkAuthorityDeleted(string(raw.get(2)), now));
                }
                if ("generation-exhausted".equals(status)) {
                    return CompletableFuture.completedFuture(
                        new ZLinkAuthorityGenerationExhausted());
                }
                if (!"stored".equals(status)) {
                    return CompletableFuture.failedFuture(
                        new IllegalStateException(
                            "unsupported authority transition: " + status));
                }
                return CompletableFuture.completedFuture(new ZLinkAuthorityStored(
                    string(raw.get(2)),
                    Base64.getDecoder().decode(payload),
                    number(raw.get(3)),
                    number(raw.get(4)),
                    string(raw.get(5)),
                    number(raw.get(6)),
                    allocation(raw, 7),
                    now));
            });
    }

    CompletionStage<ZLinkAuthorityScanResult> list(
        String prefix,
        Optional<ZLinkAuthorityScanCursor> cursor,
        int limit,
        ZLinkStoreCancellation cancellation) {
        if (prefix == null || limit < 1 || limit > 1000) {
            throw new IllegalArgumentException("authority scan limit must be 1..1000");
        }
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        Cursor decoded = cursor.map(this::decodeCursor).orElse(new Cursor("", 0));
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                LIST,
                ScriptOutputType.MULTI,
                new String[] {
                    keys.authorityIndexKey(),
                    keys.authorityRevisionKey()
                },
                decoded.revision,
                Integer.toString(decoded.offset),
                Integer.toString(limit),
                authorityRowPrefix(),
                prefix))
            .thenApply(raw -> toPage(raw));
    }

    CompletionStage<ZLinkObjectReserveResult> reserve(
        ZLinkObjectReservationRequest request,
        ZLinkStoreCancellation cancellation) {
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        String key = request.authorityKey();
        String encodedKey = keys.encodedAuthorityKey(key);
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                RESERVE,
                ScriptOutputType.MULTI,
                new String[] {
                    keys.authorityRowKey(key),
                    keys.authorityIndexKey(),
                    keys.authorityRevisionKey(),
                    keys.authorityObjectGenerationKey(),
                    keys.authorityOwnerGenerationKey(),
                    keys.leaseStateKey(),
                    keys.placementCapacityStateKey()
                },
                encodedKey,
                request.stableType(),
                request.targetOwner().ownerId(),
                Long.toString(request.targetOwner().leaseGeneration()),
                request.creationIntentReference(),
                encode(request.creationIntentHash()),
                key,
                encode(request.creatingPayload()),
                descriptorKey(request.targetDescriptor()),
                Long.toString(
                    request.targetDescriptorLifecycleGeneration()),
                Integer.toString(request.objectKind().value()),
                Integer.toString(request.pendingCapacityDelta())))
            .thenApply(raw -> reserveResult(request, raw));
    }

    CompletionStage<ZLinkObjectCommitResult> commit(
        ZLinkObjectReservation reservation,
        byte[] readyPayload,
        ZLinkStoreCancellation cancellation) {
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                COMMIT_RESERVATION,
                ScriptOutputType.MULTI,
                new String[] {
                    keys.authorityRowKey(reservation.authorityKey()),
                    keys.authorityRevisionKey(),
                    keys.leaseStateKey(),
                    keys.placementCapacityStateKey()
                },
                reservation.reservationVersion(),
                Long.toString(reservation.objectGeneration()),
                Long.toString(reservation.authorityOwnerGeneration()),
                reservation.targetOwner().ownerId(),
                Long.toString(reservation.targetOwner().leaseGeneration()),
                descriptorKey(reservation.targetDescriptor()),
                Long.toString(
                    reservation.targetDescriptorLifecycleGeneration()),
                encode(readyPayload)))
            .thenApply(raw -> switch (string(raw.getFirst())) {
                case "committed" -> ZLinkObjectCommitResult.COMMITTED;
                case "already-committed" -> ZLinkObjectCommitResult.ALREADY_COMMITTED;
                case "generation-exhausted" ->
                    ZLinkObjectCommitResult.GENERATION_EXHAUSTED;
                default -> ZLinkObjectCommitResult.STALE;
            });
    }

    CompletionStage<ZLinkObjectAbortResult> abort(
        ZLinkObjectReservation reservation,
        ZLinkStoreCancellation cancellation) {
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                ABORT_RESERVATION,
                ScriptOutputType.MULTI,
                new String[] {
                    keys.authorityRowKey(reservation.authorityKey()),
                    keys.authorityIndexKey(),
                    keys.authorityRevisionKey(),
                    keys.leaseStateKey(),
                    keys.placementCapacityStateKey()
                },
                reservation.reservationVersion(),
                Long.toString(reservation.objectGeneration()),
                Long.toString(reservation.authorityOwnerGeneration()),
                reservation.targetOwner().ownerId(),
                Long.toString(reservation.targetOwner().leaseGeneration()),
                descriptorKey(reservation.targetDescriptor()),
                Long.toString(
                    reservation.targetDescriptorLifecycleGeneration()),
                keys.encodedAuthorityKey(reservation.authorityKey())))
            .thenApply(raw -> switch (string(raw.getFirst())) {
                case "aborted" -> ZLinkObjectAbortResult.ABORTED;
                case "already-aborted" -> ZLinkObjectAbortResult.ALREADY_ABORTED;
                default -> ZLinkObjectAbortResult.STALE;
            });
    }

    CompletionStage<ZLinkRelocationCapacityReserveResult>
        reserveRelocationCapacity(
            ZLinkRelocationCapacityReservationRequest request,
            ZLinkStoreCancellation cancellation) {
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                RESERVE_RELOCATION_CAPACITY,
                ScriptOutputType.MULTI,
                new String[] {
                    keys.authorityRowKey(request.authorityKey()),
                    keys.leaseStateKey()
                },
                request.expectedStoreVersion(),
                request.sourceOwner().ownerId(),
                Long.toString(
                    request.sourceOwner().leaseGeneration()),
                request.targetOwner().ownerId(),
                Long.toString(
                    request.targetOwner().leaseGeneration()),
                Integer.toString(request.objectKind().value()),
                request.stableType(),
                descriptorKey(request.sourceDescriptor()),
                Long.toString(
                    request.sourceDescriptorLifecycleGeneration()),
                Integer.toString(request.capacityDelta())))
            .thenCompose(raw -> {
                String status = string(raw.getFirst());
                if ("conflict".equals(status)) {
                    return read(request.authorityKey(), cancellation)
                        .thenApply(
                            ZLinkRelocationCapacityConflict::new);
                }
                return CompletableFuture.completedFuture(
                    new ZLinkRelocationCapacityTargetUnavailable());
            });
    }

    CompletionStage<ZLinkRelocationCapacityAbortResult>
        abortRelocationCapacity(
            ZLinkRelocationCapacityFence fence,
            ZLinkStoreCancellation cancellation) {
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        return CompletableFuture.completedFuture(
            ZLinkRelocationCapacityAbortResult.STALE);
    }

    CompletionStage<ZLinkAggregatePrepareResult> prepareAggregate(
        ZLinkAggregatePrepareRequest request,
        ZLinkStoreCancellation cancellation) {
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        return CompletableFuture.completedFuture(
            new ZLinkAggregateConflict());
    }

    CompletionStage<ZLinkAggregateCommitResult> commitAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation) {
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        return CompletableFuture.completedFuture(
            ZLinkAggregateCommitResult.STALE);
    }

    CompletionStage<ZLinkAggregateAbortResult> abortAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation) {
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        return CompletableFuture.completedFuture(
            ZLinkAggregateAbortResult.STALE);
    }

    private ZLinkAuthorityReadResult readResult(List<Object> raw) {
        Instant now = time(raw, 1);
        if ("missing".equals(string(raw.getFirst()))) {
            return new ZLinkAuthorityMissing(now);
        }
        return snapshot(raw, 2, now);
    }

    private ZLinkObjectReserveResult reserveResult(
        ZLinkObjectReservationRequest request,
        List<Object> raw) {
        String status = string(raw.getFirst());
        Instant now = time(raw, 1);
        if ("generation-exhausted".equals(status)) {
            return new ZLinkObjectGenerationExhausted();
        }
        if ("owner-stale".equals(status)) {
            return new ZLinkObjectConflict(
                new ZLinkAuthorityMissing(now));
        }
        if ("already-exists".equals(status) || "type-mismatch".equals(status)) {
            ZLinkAuthoritySnapshot current = snapshot(raw, 2, now);
            return "already-exists".equals(status)
                ? new ZLinkObjectAlreadyExists(current)
                : new ZLinkObjectTypeMismatch(current);
        }
        if (!"reserved".equals(status)) {
            throw new IllegalStateException("unknown reserve result: " + status);
        }
        return new ZLinkObjectReserved(new ZLinkObjectReservation(
            request.authorityKey(),
            string(raw.get(2)),
            number(raw.get(3)),
            number(raw.get(4)),
            string(raw.get(5)),
            request.targetDescriptor(),
            request.targetDescriptorLifecycleGeneration(),
            request.targetOwner()));
    }

    private ZLinkAuthoritySnapshot snapshot(
        List<Object> raw,
        int offset,
        Instant now) {
        return new ZLinkAuthoritySnapshot(
            string(raw.get(offset)),
            decode(string(raw.get(offset + 1))),
            number(raw.get(offset + 2)),
            number(raw.get(offset + 3)),
            string(raw.get(offset + 4)),
            number(raw.get(offset + 5)),
            allocation(raw, offset + 6),
            now);
    }

    private ZLinkAuthorityScanResult toPage(List<Object> raw) {
        if ("expired".equals(string(raw.getFirst()))) {
            return new ZLinkAuthorityScanExpired();
        }
        Instant now = time(raw, 1);
        String revision = string(raw.get(2));
        int nextOffset = Math.toIntExact(number(raw.get(3)));
        @SuppressWarnings("unchecked")
        List<Object> values = (List<Object>) raw.get(4);
        List<ZLinkAuthorityEntry> items = new ArrayList<>(values.size() / 13);
        for (int index = 0; index + 12 < values.size(); index += 13) {
            items.add(new ZLinkAuthorityEntry(
                string(values.get(index)),
                snapshot(values, index + 1, now)));
        }
        Optional<ZLinkAuthorityScanCursor> next = nextOffset < 0
            ? Optional.empty()
            : Optional.of(encodeCursor(new Cursor(revision, nextOffset)));
        return new ZLinkAuthorityPage(items, next);
    }

    private String authorityRowPrefix() {
        return keys.authorityRowKeyPrefix();
    }

    private ZLinkAuthorityScanCursor encodeCursor(Cursor cursor) {
        String raw = cursor.revision + ":" + cursor.offset;
        return new ZLinkAuthorityScanCursor(Base64.getUrlEncoder()
            .withoutPadding()
            .encodeToString(raw.getBytes(StandardCharsets.UTF_8)));
    }

    private Cursor decodeCursor(ZLinkAuthorityScanCursor cursor) {
        try {
            String raw = new String(
                Base64.getUrlDecoder().decode(cursor.encoded()),
                StandardCharsets.UTF_8);
            int separator = raw.indexOf(':');
            return new Cursor(
                raw.substring(0, separator),
                Integer.parseInt(raw.substring(separator + 1)));
        } catch (RuntimeException error) {
            throw new IllegalArgumentException("invalid authority scan cursor", error);
        }
    }

    private static void requireKey(String key) {
        if (key == null || key.isBlank()) {
            throw new IllegalArgumentException("authority key is required");
        }
    }

    private static boolean cancelled(ZLinkStoreCancellation cancellation) {
        return java.util.Objects.requireNonNull(cancellation, "cancellation")
            .isCancellationRequested();
    }

    private static <T> CompletionStage<T> cancelledStage() {
        return CompletableFuture.failedFuture(
            new java.util.concurrent.CancellationException(
                "store operation was cancelled before I/O"));
    }

    private static String encode(byte[] value) {
        return Base64.getEncoder().encodeToString(value.clone());
    }

    private static String descriptorKey(
        ZLinkMeshNodeDescriptorKey descriptor) {
        return Base64.getUrlEncoder()
            .withoutPadding()
            .encodeToString(descriptor.meshName().getBytes(
                StandardCharsets.UTF_8))
            + ":"
            + descriptor.rid().toHex();
    }

    private static ZLinkPlacementAllocation allocation(
        List<Object> values,
        int offset) {
        return new ZLinkPlacementAllocation(
            "pending".equals(string(values.get(offset)))
                ? ZLinkPlacementAllocationState.PENDING
                : ZLinkPlacementAllocationState.ACTIVE,
            objectKind(number(values.get(offset + 1))),
            string(values.get(offset + 2)),
            descriptor(string(values.get(offset + 3))),
            number(values.get(offset + 4)),
            Math.toIntExact(number(values.get(offset + 5))));
    }

    private static ZLinkPlacementObjectKind objectKind(long value) {
        for (ZLinkPlacementObjectKind kind :
            ZLinkPlacementObjectKind.values()) {
            if (kind.value() == value) {
                return kind;
            }
        }
        throw new IllegalStateException(
            "unsupported placement object kind: " + value);
    }

    private static ZLinkMeshNodeDescriptorKey descriptor(String value) {
        int separator = value.indexOf(':');
        if (separator <= 0 || separator == value.length() - 1) {
            throw new IllegalStateException(
                "invalid stored MeshNode descriptor key");
        }
        return new ZLinkMeshNodeDescriptorKey(
            new String(
                Base64.getUrlDecoder().decode(
                    value.substring(0, separator)),
                StandardCharsets.UTF_8),
            RoutingId.fromHex(value.substring(separator + 1)));
    }

    private static byte[] decode(String value) {
        return Base64.getDecoder().decode(value);
    }

    private static Instant time(List<Object> values, int index) {
        return Instant.ofEpochMilli(number(values.get(index)));
    }

    private static long number(Object value) {
        if (value instanceof Number number) {
            return number.longValue();
        }
        return Long.parseLong(string(value));
    }

    private static String string(Object value) {
        if (value instanceof byte[] bytes) {
            return new String(bytes, StandardCharsets.UTF_8);
        }
        return value == null ? "" : value.toString();
    }

    private record Cursor(String revision, int offset) {
    }
}
