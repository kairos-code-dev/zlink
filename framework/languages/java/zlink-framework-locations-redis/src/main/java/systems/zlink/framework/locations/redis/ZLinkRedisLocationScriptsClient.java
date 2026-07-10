package systems.zlink.framework.locations.redis;

import io.lettuce.core.RedisCommandExecutionException;
import io.lettuce.core.RedisCommandTimeoutException;
import io.lettuce.core.RedisConnectionException;
import io.lettuce.core.RedisException;
import io.lettuce.core.ScriptOutputType;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteResult;
import systems.zlink.framework.locations.ZLinkOwnerLease;
import systems.zlink.framework.locations.ZLinkOwnerLeaseRenewal;
import systems.zlink.framework.locations.ZLinkOwnerLeaseSnapshot;

final class ZLinkRedisLocationScriptsClient {
    private final ZLinkRedisLocationConnection connection;
    private final ZLinkRedisLocationKeys keys;

    ZLinkRedisLocationScriptsClient(
        ZLinkRedisLocationConnection connection,
        ZLinkRedisLocationKeys keys) {
        this.connection = connection;
        this.keys = keys;
    }

    CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLeaseAsync(
        String ownerId,
        RoutingId nodeRid,
        Duration leaseTtl) {
        long ttlMs = Math.max(1L, leaseTtl.toMillis());
        return connection.commands()
            .thenCompose(redis -> redis.<Long>eval(
                ZLinkRedisLocationScripts.RENEW_LEASE,
                ScriptOutputType.INTEGER,
                new String[] {keys.leaseKey(ownerId), keys.leaseIndexKey()},
                ownerId,
                nodeRid.toHex(),
                Long.toString(ttlMs)))
            .thenApply(nowMs -> {
                Instant storeNow = fromUnixMs(nowMs);
                return new ZLinkOwnerLeaseRenewal(storeNow.plusMillis(ttlMs), storeNow);
            })
            .exceptionally(ZLinkRedisLocationScriptsClient::propagateWriteFailure);
    }

    CompletionStage<Boolean> removeOwnerLeaseAsync(String ownerId) {
        return connection.commands()
            .thenCompose(redis -> redis.<Long>eval(
                ZLinkRedisLocationScripts.REMOVE_LEASE,
                ScriptOutputType.INTEGER,
                new String[] {keys.leaseKey(ownerId), keys.leaseIndexKey()},
                ownerId))
            .thenApply(removed -> removed != null && removed > 0)
            .exceptionally(ZLinkRedisLocationScriptsClient::propagateWriteFailure);
    }

    CompletionStage<Long> removeAllByOwnerAsync(String ownerId) {
        return connection.commands()
            .thenCompose(redis -> redis.<Long>eval(
                ZLinkRedisLocationScripts.REMOVE_ALL_BY_OWNER,
                ScriptOutputType.INTEGER,
                new String[] {
                    keys.ownerIndexKeyPrefix("peer") + ownerId,
                    keys.ownerIndexKeyPrefix("spot") + ownerId,
                    keys.ownerIndexKeyPrefix("actor") + ownerId,
                    keys.ownerIndexKeyPrefix("route") + ownerId,
                    keys.kindIndexKey("peer"),
                    keys.kindIndexKey("spot"),
                    keys.kindIndexKey("actor"),
                    keys.kindIndexKey("route")
                },
                keys.rowHashKeyPrefix("peer"),
                keys.rowHashKeyPrefix("spot"),
                keys.rowHashKeyPrefix("actor"),
                keys.rowHashKeyPrefix("route"),
                keys.stampKey("peer", null),
                keys.stampKey("spot", null),
                keys.stampKey("actor", null),
                keys.stampKey("route", null)));
    }

    CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeasesAsync() {
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                ZLinkRedisLocationScripts.LIST_LEASES,
                ScriptOutputType.MULTI,
                new String[] {keys.leaseIndexKey()},
                keys.leaseKeyPrefix()))
            .thenApply(this::toLeaseSnapshot);
    }

    CompletionStage<ZLinkLocationWriteResult> write(
        String tag,
        String rowKey,
        String meshName,
        String ownerId,
        long generation,
        String json,
        ZLinkLocationWriteIntent intent) {
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                ZLinkRedisLocationScripts.WRITE,
                ScriptOutputType.MULTI,
                new String[] {keys.rowHashKey(tag, rowKey), keys.generationKey(tag, rowKey), keys.kindIndexKey(tag)},
                intentName(intent),
                ownerId,
                Long.toString(generation),
                json,
                rowKey,
                keys.leaseKeyPrefix(),
                keys.ownerIndexKeyPrefix(tag),
                keys.stampKey(tag, meshName),
                meshName == null ? "" : keys.stampKey(tag, null),
                meshName == null ? "0" : "1",
                meshName == null ? "" : meshName))
            .thenApply(ZLinkRedisLocationScriptsClient::toWriteResult)
            .exceptionally(ZLinkRedisLocationScriptsClient::propagateWriteFailure);
    }

    CompletionStage<ZLinkLocationWriteResult> remove(
        String tag,
        String rowKey,
        String meshName,
        ZLinkLocationOwnerToken owner) {
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                ZLinkRedisLocationScripts.REMOVE,
                ScriptOutputType.MULTI,
                new String[] {keys.rowHashKey(tag, rowKey), keys.kindIndexKey(tag)},
                owner.ownerId(),
                Long.toString(owner.generation()),
                rowKey,
                keys.ownerIndexKeyPrefix(tag),
                keys.stampKey(tag, meshName),
                meshName == null ? "" : keys.stampKey(tag, null)))
            .thenApply(ZLinkRedisLocationScriptsClient::toWriteResult)
            .exceptionally(ZLinkRedisLocationScriptsClient::propagateWriteFailure);
    }

    private ZLinkOwnerLeaseSnapshot toLeaseSnapshot(List<Object> raw) {
        Instant storeNow = fromUnixMs(number(raw.getFirst()));
        @SuppressWarnings("unchecked")
        List<Object> entries = (List<Object>) raw.get(1);
        List<ZLinkOwnerLease> leases = new ArrayList<>();
        for (int index = 0; index + 2 < entries.size(); index += 3) {
            String ownerId = string(entries.get(index));
            String value = string(entries.get(index + 1));
            long remainingMs = number(entries.get(index + 2));
            int separator = value.indexOf('|');
            RoutingId nodeRid = RoutingId.fromHex(value.substring(0, separator));
            Instant renewedAt = fromUnixMs(Long.parseLong(value.substring(separator + 1)));
            leases.add(new ZLinkOwnerLease(ownerId, nodeRid, storeNow.plusMillis(remainingMs), renewedAt));
        }
        return new ZLinkOwnerLeaseSnapshot(List.copyOf(leases), storeNow);
    }

    private static ZLinkLocationWriteResult toWriteResult(List<Object> result) {
        String status = string(result.get(0));
        return switch (status) {
            case "stored" -> ZLinkLocationWriteResult.stored(number(result.get(1)), fromUnixMs(number(result.get(2))));
            case "conflict" -> ZLinkLocationWriteResult.rejectedConflict();
            default -> ZLinkLocationWriteResult.ignoredStale();
        };
    }

    private static <T> T propagateWriteFailure(Throwable failure) {
        Throwable unwrapped = unwrap(failure);
        if (unwrapped instanceof RedisException
            || unwrapped instanceof RedisConnectionException
            || unwrapped instanceof RedisCommandTimeoutException
            || unwrapped instanceof RedisCommandExecutionException) {
            throw new CompletionException(unwrapped);
        }
        throw new CompletionException(unwrapped);
    }

    private static Throwable unwrap(Throwable failure) {
        Throwable current = failure;
        while ((current instanceof CompletionException || current instanceof java.util.concurrent.ExecutionException)
            && current.getCause() != null) {
            current = current.getCause();
        }
        return current;
    }

    private static String intentName(ZLinkLocationWriteIntent intent) {
        return switch (intent) {
            case NEW_CLAIM -> "new";
            case RENEW -> "renew";
            case TAKEOVER -> "takeover";
        };
    }

    private static long number(Object value) {
        if (value instanceof Number number) {
            return number.longValue();
        }
        return Long.parseLong(string(value));
    }

    private static String string(Object value) {
        return value == null ? "" : value.toString();
    }

    private static Instant fromUnixMs(long unixMs) {
        return Instant.ofEpochMilli(unixMs);
    }
}
