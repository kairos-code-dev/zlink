package systems.zlink.framework.locations.redis;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
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
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAcquireRequest;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAcquireResult;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAcquired;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocation;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocationMember;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocationSnapshot;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotGroupConfigurationMismatch;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotGroupExhausted;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotIdentityModeConflict;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotReleaseResult;

final class ZLinkRedisLocationScriptsClient {
    private static final ObjectMapper JSON = new ObjectMapper();
    private static final TypeReference<List<ZLinkRoutingIdSlotAllocationMember>> MEMBERS_TYPE =
        new TypeReference<>() { };
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

    CompletionStage<ZLinkRoutingIdSlotAcquireResult> acquireRoutingIdSlotAsync(
        ZLinkRoutingIdSlotAcquireRequest request) {
        List<ZLinkRoutingIdSlotAllocationMember> members = normalizeMembers(request.members());
        String config = serializeMembers(members);
        long ttlMs = Math.max(1L, request.leaseTtl().toMillis());
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                ZLinkRedisLocationScripts.ACQUIRE_ROUTING_ID_SLOT,
                ScriptOutputType.MULTI,
                new String[] {
                    keys.routingIdSlotGroupKey(request.groupName()),
                    keys.leaseKey(request.ownerId()),
                    keys.leaseIndexKey()
                },
                config,
                Integer.toString(request.slotCount()),
                request.ownerId(),
                Long.toString(ttlMs),
                keys.leaseKeyPrefix()))
            .thenApply(raw -> toSlotAcquireResult(request, members, raw))
            .exceptionally(ZLinkRedisLocationScriptsClient::propagateWriteFailure);
    }

    CompletionStage<ZLinkRoutingIdSlotReleaseResult> releaseRoutingIdSlotAsync(
        String groupName,
        int slot,
        ZLinkLocationOwnerToken owner) {
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                ZLinkRedisLocationScripts.RELEASE_ROUTING_ID_SLOT,
                ScriptOutputType.MULTI,
                new String[] {keys.routingIdSlotGroupKey(groupName)},
                Integer.toString(slot),
                owner.ownerId(),
                Long.toString(owner.generation())))
            .thenApply(raw -> "released".equals(string(raw.getFirst()))
                ? ZLinkRoutingIdSlotReleaseResult.RELEASED
                : ZLinkRoutingIdSlotReleaseResult.IGNORED_STALE)
            .exceptionally(ZLinkRedisLocationScriptsClient::propagateWriteFailure);
    }

    CompletionStage<ZLinkRoutingIdSlotAllocationSnapshot> listRoutingIdSlotsAsync(
        String groupName) {
        return connection.commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                ZLinkRedisLocationScripts.LIST_ROUTING_ID_SLOTS,
                ScriptOutputType.MULTI,
                new String[] {keys.routingIdSlotGroupKey(groupName)},
                keys.leaseKeyPrefix()))
            .thenApply(raw -> toSlotSnapshot(groupName, raw))
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

    private static ZLinkRoutingIdSlotAcquireResult toSlotAcquireResult(
        ZLinkRoutingIdSlotAcquireRequest request,
        List<ZLinkRoutingIdSlotAllocationMember> members,
        List<Object> raw) {
        String status = string(raw.getFirst());
        return switch (status) {
            case "acquired" -> {
                Instant storeNow = fromUnixMs(number(raw.get(4)));
                yield new ZLinkRoutingIdSlotAcquired(new ZLinkRoutingIdSlotAllocation(
                    Math.toIntExact(number(raw.get(1))),
                    new ZLinkLocationOwnerToken(request.ownerId(), number(raw.get(2))),
                    fromUnixMs(number(raw.get(3))),
                    storeNow));
            }
            case "exhausted" -> new ZLinkRoutingIdSlotGroupExhausted();
            case "identity-conflict" -> new ZLinkRoutingIdSlotIdentityModeConflict();
            case "mismatch" -> new ZLinkRoutingIdSlotGroupConfigurationMismatch(
                members,
                request.slotCount(),
                deserializeMembers(string(raw.get(1))),
                Math.toIntExact(number(raw.get(2))));
            default -> throw new IllegalStateException(
                "Unknown routing ID slot acquire result: " + status);
        };
    }

    private static ZLinkRoutingIdSlotAllocationSnapshot toSlotSnapshot(
        String groupName,
        List<Object> raw) {
        String config = string(raw.getFirst());
        int slotCount = Math.toIntExact(number(raw.get(1)));
        Instant storeNow = fromUnixMs(number(raw.get(2)));
        @SuppressWarnings("unchecked")
        List<Object> entries = (List<Object>) raw.get(3);
        List<ZLinkRoutingIdSlotAllocation> allocations = new ArrayList<>();
        for (int index = 0; index + 3 < entries.size(); index += 4) {
            allocations.add(new ZLinkRoutingIdSlotAllocation(
                Math.toIntExact(number(entries.get(index))),
                new ZLinkLocationOwnerToken(
                    string(entries.get(index + 1)),
                    number(entries.get(index + 2))),
                fromUnixMs(number(entries.get(index + 3))),
                storeNow));
        }
        return new ZLinkRoutingIdSlotAllocationSnapshot(
            groupName,
            config.isEmpty() ? List.of() : deserializeMembers(config),
            slotCount,
            allocations,
            storeNow);
    }

    private static List<ZLinkRoutingIdSlotAllocationMember> normalizeMembers(
        List<ZLinkRoutingIdSlotAllocationMember> members) {
        return members.stream()
            .sorted(java.util.Comparator.comparing(ZLinkRoutingIdSlotAllocationMember::meshName)
                .thenComparing(ZLinkRoutingIdSlotAllocationMember::routingIdPrefix))
            .toList();
    }

    private static String serializeMembers(List<ZLinkRoutingIdSlotAllocationMember> members) {
        try {
            return JSON.writeValueAsString(members);
        } catch (java.io.IOException error) {
            throw new IllegalArgumentException("routing ID allocation members are invalid", error);
        }
    }

    private static List<ZLinkRoutingIdSlotAllocationMember> deserializeMembers(String value) {
        try {
            return List.copyOf(JSON.readValue(value, MEMBERS_TYPE));
        } catch (java.io.IOException error) {
            throw new IllegalStateException("stored routing ID allocation members are invalid", error);
        }
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
