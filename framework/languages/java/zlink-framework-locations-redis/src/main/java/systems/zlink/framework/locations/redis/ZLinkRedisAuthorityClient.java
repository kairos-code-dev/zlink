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
            redis.call('HGET', KEYS[1], 'ownerLeaseGeneration')}
        """;
    private static final String CAS = PROLOGUE + """
        local exists = redis.call('EXISTS', KEYS[1]) == 1
        if ARGV[1] == 'missing' then
            if exists then
                return {'conflict', nowMs}
            end
        elseif not exists or redis.call('HGET', KEYS[1], 'version') ~= ARGV[2] then
            return {'conflict', nowMs}
        end
        if ARGV[3] == 'delete' then
            if redis.call('GET', KEYS[3]) == '9223372036854775807' then
                return {'generation-exhausted', nowMs}
            end
            local revision = redis.call('INCR', KEYS[3])
            redis.call('DEL', KEYS[1])
            redis.call('ZREM', KEYS[2], ARGV[6])
            return {'deleted', nowMs, revision}
        end
        if ARGV[4] ~= 'preserve' then
            return {'unsupported-owner-transition', nowMs}
        end
        if not exists then
            return {'unsupported-new-object', nowMs}
        end
        if redis.call('GET', KEYS[3]) == '9223372036854775807' then
            return {'generation-exhausted', nowMs}
        end
        local revision = redis.call('INCR', KEYS[3])
        redis.call('HSET', KEYS[1],
            'version', revision,
            'payload', ARGV[5])
        return {'stored', nowMs, revision,
            redis.call('HGET', KEYS[1], 'objectGeneration'),
            redis.call('HGET', KEYS[1], 'ownerGeneration')}
        """;
    private static final String RESERVE = PROLOGUE + """
        if redis.call('EXISTS', KEYS[1]) == 1 then
            local status = redis.call('HGET', KEYS[1], 'stableType') == ARGV[2]
                and 'already-exists' or 'type-mismatch'
            return {status, nowMs,
                redis.call('HGET', KEYS[1], 'version'),
                redis.call('HGET', KEYS[1], 'payload'),
                redis.call('HGET', KEYS[1], 'objectGeneration'),
                redis.call('HGET', KEYS[1], 'ownerGeneration'),
                redis.call('HGET', KEYS[1], 'ownerId'),
                redis.call('HGET', KEYS[1], 'ownerLeaseGeneration')}
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
            'payload', '',
            'objectGeneration', objectGeneration,
            'ownerGeneration', ownerGeneration,
            'ownerId', ARGV[3],
            'ownerLeaseGeneration', ARGV[4],
            'stableType', ARGV[2],
            'authorityKey', ARGV[7],
            'reservationVersion', reservationVersion,
            'creationIntentReference', ARGV[5],
            'creationIntentHash', ARGV[6])
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
            return {'already-committed', nowMs}
        end
        if currentReservation ~= ARGV[1]
            or redis.call('HGET', KEYS[1], 'objectGeneration') ~= ARGV[2]
            or redis.call('HGET', KEYS[1], 'ownerGeneration') ~= ARGV[3]
            or redis.call('HGET', KEYS[1], 'ownerId') ~= ARGV[4]
            or redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') ~= ARGV[5] then
            return {'stale', nowMs}
        end
        if redis.call('GET', KEYS[2]) == '9223372036854775807' then
            return {'generation-exhausted', nowMs}
        end
        local revision = redis.call('INCR', KEYS[2])
        redis.call('HSET', KEYS[1], 'version', revision, 'payload', ARGV[6])
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
            or redis.call('HGET', KEYS[1], 'ownerLeaseGeneration') ~= ARGV[5] then
            return {'stale', nowMs}
        end
        redis.call('DEL', KEYS[1])
        redis.call('ZREM', KEYS[2], ARGV[6])
        redis.call('INCR', KEYS[3])
        return {'aborted', nowMs}
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
        if (mutation instanceof ZLinkAuthorityPut put
            && put.generationTransition()
                != ZLinkAuthorityGenerationTransition.PRESERVE) {
            return CompletableFuture.failedFuture(new IllegalArgumentException(
                "Java authority mutation has no target owner token for "
                    + put.generationTransition()));
        }
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        String expectationKind = expectation instanceof ZLinkAuthorityExpectMissing
            ? "missing"
            : "found";
        String version = expectation instanceof ZLinkAuthorityExpectFound found
            ? found.storeVersion()
            : "";
        boolean deleting = mutation instanceof ZLinkAuthorityDelete;
        String payload = deleting
            ? ""
            : encode(((ZLinkAuthorityPut) mutation).payload());
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                CAS,
                ScriptOutputType.MULTI,
                new String[] {
                    keys.authorityRowKey(key),
                    keys.authorityIndexKey(),
                    keys.authorityRevisionKey()
                },
                expectationKind,
                version,
                deleting ? "delete" : "put",
                "preserve",
                payload,
                keys.encodedAuthorityKey(key)))
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
                    keys.authorityOwnerGenerationKey()
                },
                encodedKey,
                request.stableType(),
                request.targetOwner().ownerId(),
                Long.toString(request.targetOwner().generation()),
                request.creationIntentReference(),
                encode(request.creationIntentHash()),
                key))
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
                    keys.authorityRevisionKey()
                },
                reservation.reservationVersion(),
                Long.toString(reservation.objectGeneration()),
                Long.toString(reservation.authorityOwnerGeneration()),
                reservation.targetOwner().ownerId(),
                Long.toString(reservation.targetOwner().generation()),
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
                    keys.authorityRevisionKey()
                },
                reservation.reservationVersion(),
                Long.toString(reservation.objectGeneration()),
                Long.toString(reservation.authorityOwnerGeneration()),
                reservation.targetOwner().ownerId(),
                Long.toString(reservation.targetOwner().generation()),
                keys.encodedAuthorityKey(reservation.authorityKey())))
            .thenApply(raw -> switch (string(raw.getFirst())) {
                case "aborted" -> ZLinkObjectAbortResult.ABORTED;
                case "already-aborted" -> ZLinkObjectAbortResult.ALREADY_ABORTED;
                default -> ZLinkObjectAbortResult.STALE;
            });
    }

    CompletionStage<ZLinkAggregatePrepareResult> prepareAggregate(
        ZLinkAggregatePrepareRequest request,
        ZLinkStoreCancellation cancellation) {
        return unsupportedAggregate(
            "aggregate prepare requires the missing exact owner lease fence");
    }

    CompletionStage<ZLinkAggregateCommitResult> commitAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation) {
        return unsupportedAggregate(
            "aggregate commit requires a prepared owner lease fence");
    }

    CompletionStage<ZLinkAggregateAbortResult> abortAggregate(
        ZLinkAggregateFence fence,
        ZLinkStoreCancellation cancellation) {
        return unsupportedAggregate(
            "aggregate abort requires a prepared owner lease fence");
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
        List<ZLinkAuthorityEntry> items = new ArrayList<>(values.size() / 7);
        for (int index = 0; index + 6 < values.size(); index += 7) {
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

    private static <T> CompletionStage<T> unsupportedAggregate(String message) {
        return CompletableFuture.failedFuture(
            new IllegalStateException(message));
    }

    private static String encode(byte[] value) {
        return Base64.getEncoder().encodeToString(value.clone());
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
