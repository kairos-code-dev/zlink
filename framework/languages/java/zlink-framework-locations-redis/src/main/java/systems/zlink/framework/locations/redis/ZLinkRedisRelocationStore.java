package systems.zlink.framework.locations.redis;

import io.lettuce.core.ScriptOutputType;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.time.Duration;
import java.time.Instant;
import java.util.Base64;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.zip.CRC32C;
import systems.zlink.framework.locations.ZLinkRelocationDeleteResult;
import systems.zlink.framework.locations.ZLinkRelocationFound;
import systems.zlink.framework.locations.ZLinkRelocationMissing;
import systems.zlink.framework.locations.ZLinkRelocationReadResult;
import systems.zlink.framework.locations.ZLinkRelocationRenewMissing;
import systems.zlink.framework.locations.ZLinkRelocationRenewResult;
import systems.zlink.framework.locations.ZLinkRelocationRenewed;
import systems.zlink.framework.locations.ZLinkRelocationStore;
import systems.zlink.framework.locations.ZLinkRelocationStored;
import systems.zlink.framework.locations.ZLinkStoreCancellation;

public final class ZLinkRedisRelocationStore
    implements ZLinkRelocationStore, AutoCloseable {
    private static final int MAX_COMPONENT_BYTES = 64 * 1024 * 1024;
    private static final String PUT = """
        if redis.replicate_commands then redis.replicate_commands() end
        local time = redis.call('TIME')
        local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
        local current = redis.call('GET', KEYS[1])
        if current and current ~= ARGV[1] then
            return {'collision', nowMs}
        end
        redis.call('SET', KEYS[1], ARGV[1], 'PX', ARGV[2])
        return {'stored', nowMs}
        """;
    private static final String RENEW = """
        if redis.replicate_commands then redis.replicate_commands() end
        local time = redis.call('TIME')
        local nowMs = tonumber(time[1]) * 1000 + math.floor(tonumber(time[2]) / 1000)
        if redis.call('EXISTS', KEYS[1]) == 0 then
            return {'missing', nowMs}
        end
        redis.call('PEXPIRE', KEYS[1], ARGV[1])
        return {'renewed', nowMs}
        """;

    private final ZLinkRedisLocationConnection connection;
    private final String keyPrefix;

    public ZLinkRedisRelocationStore(ZLinkRedisRelocationOptions options) {
        ZLinkRedisRelocationOptions validated = Objects.requireNonNull(options, "options");
        validated.validate();
        connection = new ZLinkRedisLocationConnection(validated.redisUri());
        keyPrefix = validated.keyPrefix() + ":{relocation}:root:";
    }

    @Override
    public CompletionStage<ZLinkRelocationStored> put(
        byte[] payload,
        Duration retention,
        ZLinkStoreCancellation cancellation) {
        byte[] snapshot = requirePayload(payload);
        long retentionMs = retentionMillis(retention);
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        String reference = sha256Hex(snapshot);
        long checksum = crc32c(snapshot);
        String encoded = Base64.getEncoder().encodeToString(snapshot);
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                PUT,
                ScriptOutputType.MULTI,
                new String[] {key(reference)},
                encoded,
                Long.toString(retentionMs)))
            .thenApply(raw -> {
                String status = string(raw.getFirst());
                if (!"stored".equals(status)) {
                    throw new IllegalStateException(
                        "relocation content reference collision: " + reference);
                }
                Instant storeNow = Instant.ofEpochMilli(number(raw.get(1)));
                return new ZLinkRelocationStored(
                    reference,
                    checksum,
                    storeNow.plusMillis(retentionMs),
                    storeNow);
            });
    }

    @Override
    public CompletionStage<ZLinkRelocationReadResult> get(
        String reference,
        ZLinkStoreCancellation cancellation) {
        String normalized = requireReference(reference);
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        return connection.commands()
            .thenCompose(redis -> redis.get(key(normalized)))
            .thenApply(value -> value == null
                ? new ZLinkRelocationMissing()
                : new ZLinkRelocationFound(Base64.getDecoder().decode(value)));
    }

    @Override
    public CompletionStage<ZLinkRelocationRenewResult> renew(
        String reference,
        Duration retention,
        ZLinkStoreCancellation cancellation) {
        String normalized = requireReference(reference);
        long retentionMs = retentionMillis(retention);
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                RENEW,
                ScriptOutputType.MULTI,
                new String[] {key(normalized)},
                Long.toString(retentionMs)))
            .thenApply(raw -> {
                Instant storeNow = Instant.ofEpochMilli(number(raw.get(1)));
                return "missing".equals(string(raw.getFirst()))
                    ? new ZLinkRelocationRenewMissing()
                    : new ZLinkRelocationRenewed(
                        storeNow.plusMillis(retentionMs),
                        storeNow);
            });
    }

    @Override
    public CompletionStage<ZLinkRelocationDeleteResult> delete(
        String reference,
        ZLinkStoreCancellation cancellation) {
        String normalized = requireReference(reference);
        if (cancelled(cancellation)) {
            return cancelledStage();
        }
        return connection.commands()
            .thenCompose(redis -> redis.del(key(normalized)))
            .thenApply(count -> count != null && count > 0
                ? ZLinkRelocationDeleteResult.DELETED
                : ZLinkRelocationDeleteResult.MISSING);
    }

    @Override
    public void close() {
        connection.closeAsync().toCompletableFuture().join();
    }

    private String key(String reference) {
        return keyPrefix + reference;
    }

    private static byte[] requirePayload(byte[] payload) {
        byte[] snapshot = Objects.requireNonNull(payload, "payload").clone();
        if (snapshot.length > MAX_COMPONENT_BYTES) {
            throw new IllegalArgumentException(
                "relocation Store component exceeds 64 MiB");
        }
        return snapshot;
    }

    private static String requireReference(String value) {
        Objects.requireNonNull(value, "reference");
        if (value.length() != 64
            || !value.chars().allMatch(character ->
                character >= '0' && character <= '9'
                    || character >= 'a' && character <= 'f')) {
            throw new IllegalArgumentException("invalid relocation reference");
        }
        return value;
    }

    private static long retentionMillis(Duration retention) {
        Objects.requireNonNull(retention, "retention");
        if (retention.isZero() || retention.isNegative()) {
            throw new IllegalArgumentException("retention must be positive");
        }
        try {
            return Math.max(1L, retention.toMillis());
        } catch (ArithmeticException error) {
            throw new IllegalArgumentException("retention is too large", error);
        }
    }

    private static boolean cancelled(ZLinkStoreCancellation cancellation) {
        return Objects.requireNonNull(cancellation, "cancellation")
            .isCancellationRequested();
    }

    private static <T> CompletionStage<T> cancelledStage() {
        return CompletableFuture.failedFuture(
            new java.util.concurrent.CancellationException(
                "store operation was cancelled before I/O"));
    }

    private static String sha256Hex(byte[] payload) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256").digest(payload);
            StringBuilder encoded = new StringBuilder(digest.length * 2);
            for (byte value : digest) {
                encoded.append(Character.forDigit((value >>> 4) & 0x0f, 16));
                encoded.append(Character.forDigit(value & 0x0f, 16));
            }
            return encoded.toString();
        } catch (java.security.NoSuchAlgorithmException error) {
            throw new IllegalStateException("SHA-256 is unavailable", error);
        }
    }

    private static long crc32c(byte[] payload) {
        CRC32C checksum = new CRC32C();
        checksum.update(payload, 0, payload.length);
        return checksum.getValue();
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
}
