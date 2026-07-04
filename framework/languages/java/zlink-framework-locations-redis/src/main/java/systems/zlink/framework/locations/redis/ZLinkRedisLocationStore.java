package systems.zlink.framework.locations.redis;

import io.lettuce.core.RedisClient;
import io.lettuce.core.RedisCommandExecutionException;
import io.lettuce.core.RedisCommandTimeoutException;
import io.lettuce.core.RedisConnectionException;
import io.lettuce.core.RedisException;
import io.lettuce.core.RedisURI;
import io.lettuce.core.ScanArgs;
import io.lettuce.core.ScanCursor;
import io.lettuce.core.ScriptOutputType;
import io.lettuce.core.ValueScanCursor;
import io.lettuce.core.api.StatefulRedisConnection;
import io.lettuce.core.api.async.RedisAsyncCommands;
import io.lettuce.core.codec.StringCodec;
import io.lettuce.core.KeyValue;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Function;
import java.util.function.Predicate;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationFilter;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationChangeStampScope;
import systems.zlink.framework.locations.ZLinkLocationChangeStampStore;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteResult;
import systems.zlink.framework.locations.ZLinkOwnerLease;
import systems.zlink.framework.locations.ZLinkOwnerLeaseRenewal;
import systems.zlink.framework.locations.ZLinkOwnerLeaseSnapshot;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationFilter;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationFilter;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;

public final class ZLinkRedisLocationStore implements
    ZLinkLocationStore,
    ZLinkLocationChangeStampStore,
    AutoCloseable {

    private static final String[] ROW_FIELDS = {"json", "gen", "updatedAtMs"};

    private final ZLinkRedisLocationOptions options;
    private final String prefix;
    private final RedisClient client;
    private final AtomicReference<CompletableFuture<StatefulRedisConnection<String, String>>> connection =
        new AtomicReference<>();

    public ZLinkRedisLocationStore(ZLinkRedisLocationOptions options) {
        this.options = Objects.requireNonNull(options, "options");
        this.options.validate();
        this.prefix = options.keyPrefix();
        this.client = RedisClient.create(options.redisUri());
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent) {
        return write(
            "peer",
            ZLinkRedisLocationKeyCodec.encodePeerKey(new ZLinkPeerLocationKey(
                peer.autoConnectType(), peer.meshName(), peer.role(), peer.nodeRid(), peer.endpoint())),
            peer.meshName(),
            peer.ownerId(),
            peer.generation(),
            ZLinkRedisLocationRowJson.serializePeer(peer),
            intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removePeerAsync(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return remove("peer", ZLinkRedisLocationKeyCodec.encodePeerKey(key), key.meshName(), owner);
    }

    @Override
    public CompletionStage<List<ZLinkPeerLocation>> listPeerLocationsAsync(ZLinkPeerLocationFilter filter) {
        return listRows("peer", ZLinkRedisLocationRowJson::deserializePeer)
            .thenApply(rows -> rows.stream().filter(row -> matches(row, filter)).toList());
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent) {
        return write(
            "spot",
            ZLinkRedisLocationKeyCodec.encodeSpotKey(new ZLinkSpotLocationKey(spot.meshName(), spot.spotRid())),
            spot.meshName(),
            spot.ownerId(),
            spot.generation(),
            ZLinkRedisLocationRowJson.serializeSpot(spot),
            intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return remove("spot", ZLinkRedisLocationKeyCodec.encodeSpotKey(key), key.meshName(), owner);
    }

    @Override
    public CompletionStage<ZLinkSpotLocation> resolveSpotAsync(ZLinkSpotLocationKey key) {
        return resolve("spot", ZLinkRedisLocationKeyCodec.encodeSpotKey(key), ZLinkRedisLocationRowJson::deserializeSpot);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocationsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page) {
        return listPage("spot", ZLinkRedisLocationRowJson::deserializeSpot, row -> matches(row, filter), page);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent) {
        return write(
            "actor",
            ZLinkRedisLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey(actor.actorId())),
            null,
            actor.ownerId(),
            actor.generation(),
            ZLinkRedisLocationRowJson.serializeActor(actor),
            intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeActorAsync(
        ZLinkActorLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return remove("actor", ZLinkRedisLocationKeyCodec.encodeActorKey(key), null, owner);
    }

    @Override
    public CompletionStage<ZLinkActorLocation> resolveActorAsync(ZLinkActorLocationKey key) {
        return resolve("actor", ZLinkRedisLocationKeyCodec.encodeActorKey(key), ZLinkRedisLocationRowJson::deserializeActor);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocationsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page) {
        return listPage("actor", ZLinkRedisLocationRowJson::deserializeActor, row -> matches(row, filter), page);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> updateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent) {
        return write(
            "route",
            ZLinkRedisLocationKeyCodec.encodeRouteKey(new ZLinkRouteLocationKey(route.routeKind(), route.routeKey())),
            null,
            route.ownerId(),
            route.generation(),
            ZLinkRedisLocationRowJson.serializeRoute(route),
            intent);
    }

    @Override
    public CompletionStage<ZLinkLocationWriteResult> removeRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner) {
        return remove("route", ZLinkRedisLocationKeyCodec.encodeRouteKey(key), null, owner);
    }

    @Override
    public CompletionStage<ZLinkRouteLocation> resolveRouteAsync(ZLinkRouteLocationKey key) {
        return resolve("route", ZLinkRedisLocationKeyCodec.encodeRouteKey(key), ZLinkRedisLocationRowJson::deserializeRoute);
    }

    @Override
    public CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocationsAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page) {
        return listPage("route", ZLinkRedisLocationRowJson::deserializeRoute, row -> matches(row, filter), page);
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseRenewal> renewOwnerLeaseAsync(
        String ownerId,
        RoutingId nodeRid,
        Duration leaseTtl) {
        long ttlMs = Math.max(1L, leaseTtl.toMillis());
        return commands()
            .thenCompose(redis -> redis.<Long>eval(
                ZLinkRedisLocationScripts.RENEW_LEASE,
                ScriptOutputType.INTEGER,
                new String[] {leaseKey(ownerId), leaseIndexKey()},
                ownerId,
                nodeRid.toHex(),
                Long.toString(ttlMs)))
            .thenApply(nowMs -> {
                Instant storeNow = fromUnixMs(nowMs);
                return new ZLinkOwnerLeaseRenewal(storeNow.plusMillis(ttlMs), storeNow);
            })
            .exceptionally(ZLinkRedisLocationStore::propagateWriteFailure);
    }

    @Override
    public CompletionStage<Boolean> removeOwnerLeaseAsync(String ownerId) {
        return commands()
            .thenCompose(redis -> redis.<Long>eval(
                ZLinkRedisLocationScripts.REMOVE_LEASE,
                ScriptOutputType.INTEGER,
                new String[] {leaseKey(ownerId), leaseIndexKey()},
                ownerId))
            .thenApply(removed -> removed != null && removed > 0)
            .exceptionally(ZLinkRedisLocationStore::propagateWriteFailure);
    }

    @Override
    public CompletionStage<Long> removeAllByOwnerAsync(String ownerId) {
        return commands()
            .thenCompose(redis -> redis.<Long>eval(
                ZLinkRedisLocationScripts.REMOVE_ALL_BY_OWNER,
                ScriptOutputType.INTEGER,
                new String[] {
                    ownerIndexKeyPrefix("peer") + ownerId,
                    ownerIndexKeyPrefix("spot") + ownerId,
                    ownerIndexKeyPrefix("actor") + ownerId,
                    ownerIndexKeyPrefix("route") + ownerId,
                    kindIndexKey("peer"),
                    kindIndexKey("spot"),
                    kindIndexKey("actor"),
                    kindIndexKey("route")
                },
                rowHashKeyPrefix("peer"),
                rowHashKeyPrefix("spot"),
                rowHashKeyPrefix("actor"),
                rowHashKeyPrefix("route"),
                stampKey("peer", null),
                stampKey("spot", null),
                stampKey("actor", null),
                stampKey("route", null)));
    }

    @Override
    public CompletionStage<ZLinkOwnerLeaseSnapshot> listOwnerLeasesAsync() {
        return commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                ZLinkRedisLocationScripts.LIST_LEASES,
                ScriptOutputType.MULTI,
                new String[] {leaseIndexKey()},
                leaseKeyPrefix()))
            .thenApply(this::toLeaseSnapshot);
    }

    @Override
    public CompletionStage<Long> getChangeStampAsync(ZLinkLocationChangeStampScope scope) {
        return commands()
            .thenCompose(redis -> redis.get(stampKey(tagOf(scope.kind()), scope.meshName())))
            .thenApply(value -> value == null ? 0L : Long.parseLong(value));
    }

    public CompletionStage<Void> closeAsync() {
        CompletableFuture<StatefulRedisConnection<String, String>> current = connection.getAndSet(null);
        CompletionStage<Void> closed = current == null
            ? CompletableFuture.completedFuture(null)
            : current.thenCompose(StatefulRedisConnection::closeAsync);
        return closed.thenCompose(ignored -> client.shutdownAsync()).thenApply(ignored -> null);
    }

    @Override
    public void close() {
        closeAsync();
    }

    private CompletionStage<ZLinkLocationWriteResult> write(
        String tag,
        String rowKey,
        String meshName,
        String ownerId,
        long generation,
        String json,
        ZLinkLocationWriteIntent intent) {
        return commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                ZLinkRedisLocationScripts.WRITE,
                ScriptOutputType.MULTI,
                new String[] {rowHashKey(tag, rowKey), generationKey(tag, rowKey), kindIndexKey(tag)},
                intentName(intent),
                ownerId,
                Long.toString(generation),
                json,
                rowKey,
                leaseKeyPrefix(),
                ownerIndexKeyPrefix(tag),
                stampKey(tag, meshName),
                meshName == null ? "" : stampKey(tag, null),
                meshName == null ? "0" : "1",
                meshName == null ? "" : meshName))
            .thenApply(ZLinkRedisLocationStore::toWriteResult)
            .exceptionally(ZLinkRedisLocationStore::propagateWriteFailure);
    }

    private CompletionStage<ZLinkLocationWriteResult> remove(
        String tag,
        String rowKey,
        String meshName,
        ZLinkLocationOwnerToken owner) {
        return commands()
            .thenCompose(redis -> redis.<List<Object>>eval(
                ZLinkRedisLocationScripts.REMOVE,
                ScriptOutputType.MULTI,
                new String[] {rowHashKey(tag, rowKey), kindIndexKey(tag)},
                owner.ownerId(),
                Long.toString(owner.generation()),
                rowKey,
                ownerIndexKeyPrefix(tag),
                stampKey(tag, meshName),
                meshName == null ? "" : stampKey(tag, null)))
            .thenApply(ZLinkRedisLocationStore::toWriteResult)
            .exceptionally(ZLinkRedisLocationStore::propagateWriteFailure);
    }

    private <T> CompletionStage<T> resolve(String tag, String rowKey, RowDeserializer<T> deserializer) {
        return commands()
            .thenCompose(redis -> redis.hmget(rowHashKey(tag, rowKey), ROW_FIELDS))
            .thenApply(fields -> materialize(fields, deserializer));
    }

    private <T> CompletionStage<List<T>> listRows(String tag, RowDeserializer<T> deserializer) {
        return commands()
            .thenCompose(redis -> redis.smembers(kindIndexKey(tag))
                .thenCompose(rowKeys -> loadRows(redis, tag, rowKeys, deserializer)));
    }

    private <T> CompletionStage<ZLinkLocationPage<T>> listPage(
        String tag,
        RowDeserializer<T> deserializer,
        Predicate<T> matches,
        ZLinkPageRequest page) {
        ZLinkPageRequest safePage = page == null ? ZLinkPageRequest.firstPage() : page;
        if (safePage.pageSize() <= 0) {
            return listRows(tag, deserializer)
                .thenApply(rows -> new ZLinkLocationPage<>(
                    rows.stream().filter(matches).toList(),
                    null));
        }

        String cursor = safePage.continuationToken() == null || safePage.continuationToken().isBlank()
            ? "0"
            : safePage.continuationToken();
        ScanArgs scanArgs = ScanArgs.Builder.limit(safePage.pageSize());
        return commands()
            .thenCompose(redis -> redis.sscan(kindIndexKey(tag), ScanCursor.of(cursor), scanArgs)
                .thenCompose(scan -> loadRows(redis, tag, scan.getValues(), deserializer)
                    .thenApply(rows -> toScannedPage(scan, rows, matches))));
    }

    private <T> CompletionStage<List<T>> loadRows(
        RedisAsyncCommands<String, String> redis,
        String tag,
        Collection<String> rowKeys,
        RowDeserializer<T> deserializer) {
        List<String> ordered = rowKeys.stream().sorted(Comparator.naturalOrder()).toList();
        List<CompletableFuture<T>> reads = ordered.stream()
            .map(rowKey -> redis.hmget(rowHashKey(tag, rowKey), ROW_FIELDS)
                .thenApply(fields -> materialize(fields, deserializer))
                .toCompletableFuture())
            .toList();
        return CompletableFuture.allOf(reads.toArray(CompletableFuture[]::new))
            .thenApply(ignored -> {
                List<T> rows = new ArrayList<>(reads.size());
                for (CompletableFuture<T> read : reads) {
                    T row = read.getNow(null);
                    if (row != null) {
                        rows.add(row);
                    }
                }
                return List.copyOf(rows);
            });
    }

    private static <T> ZLinkLocationPage<T> toScannedPage(
        ValueScanCursor<String> scan,
        List<T> rows,
        Predicate<T> matches) {
        String continuation = scan.isFinished() ? null : scan.getCursor();
        return new ZLinkLocationPage<>(
            rows.stream().filter(matches).toList(),
            continuation);
    }

    private <T> T materialize(List<KeyValue<String, String>> fields, RowDeserializer<T> deserializer) {
        String json = field(fields, "json");
        if (json == null) {
            return null;
        }
        long generation = Long.parseLong(field(fields, "gen"));
        Instant updatedAt = fromUnixMs(Long.parseLong(field(fields, "updatedAtMs")));
        return deserializer.deserialize(json, generation, updatedAt);
    }

    private CompletionStage<RedisAsyncCommands<String, String>> commands() {
        return connection().thenApply(StatefulRedisConnection::async);
    }

    private CompletionStage<StatefulRedisConnection<String, String>> connection() {
        CompletableFuture<StatefulRedisConnection<String, String>> existing = connection.get();
        if (existing != null) {
            return existing;
        }

        CompletableFuture<StatefulRedisConnection<String, String>> created =
            client.connectAsync(StringCodec.UTF8, options.redisUri()).toCompletableFuture();
        created.whenComplete((ignored, failure) -> {
            if (failure != null) {
                connection.compareAndSet(created, null);
            }
        });
        if (connection.compareAndSet(null, created)) {
            return created;
        }
        return connection.get();
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

    private static String field(List<KeyValue<String, String>> fields, String name) {
        return fields.stream()
            .filter(field -> Objects.equals(field.getKey(), name))
            .findFirst()
            .filter(KeyValue::hasValue)
            .map(KeyValue::getValue)
            .orElse(null);
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

    private static boolean matches(ZLinkPeerLocation row, ZLinkPeerLocationFilter filter) {
        ZLinkPeerLocationFilter safeFilter = filter == null ? ZLinkPeerLocationFilter.all() : filter;
        return (safeFilter.autoConnectType() == null || row.autoConnectType() == safeFilter.autoConnectType())
            && (safeFilter.meshName() == null || Objects.equals(row.meshName(), safeFilter.meshName()))
            && (safeFilter.role() == null || row.role() == safeFilter.role())
            && (safeFilter.nodeRid() == null || Objects.equals(row.nodeRid(), safeFilter.nodeRid()))
            && (safeFilter.endpoint() == null || Objects.equals(row.endpoint(), safeFilter.endpoint()));
    }

    private static boolean matches(ZLinkSpotLocation row, ZLinkSpotLocationFilter filter) {
        ZLinkSpotLocationFilter safeFilter = filter == null ? ZLinkSpotLocationFilter.all() : filter;
        return (safeFilter.meshName() == null || Objects.equals(row.meshName(), safeFilter.meshName()))
            && (safeFilter.spotType() == null || Objects.equals(row.spotType(), safeFilter.spotType()))
            && (safeFilter.nodeRid() == null || Objects.equals(row.nodeRid(), safeFilter.nodeRid()))
            && (safeFilter.spotKind() == null || row.spotKind() == safeFilter.spotKind());
    }

    private static boolean matches(ZLinkActorLocation row, ZLinkActorLocationFilter filter) {
        ZLinkActorLocationFilter safeFilter = filter == null ? ZLinkActorLocationFilter.all() : filter;
        return (safeFilter.actorType() == null || Objects.equals(row.actorType(), safeFilter.actorType()))
            && (safeFilter.nodeRid() == null || Objects.equals(row.nodeRid(), safeFilter.nodeRid()))
            && (safeFilter.spotRid() == null || Objects.equals(row.spotRid(), safeFilter.spotRid()))
            && (safeFilter.locationKind() == null || row.locationKind() == safeFilter.locationKind());
    }

    private static boolean matches(ZLinkRouteLocation row, ZLinkRouteLocationFilter filter) {
        ZLinkRouteLocationFilter safeFilter = filter == null ? ZLinkRouteLocationFilter.all() : filter;
        return (safeFilter.routeKind() == null || row.routeKind() == safeFilter.routeKind())
            && (safeFilter.ownerNodeRid() == null || Objects.equals(row.ownerNodeRid(), safeFilter.ownerNodeRid()))
            && (safeFilter.ownerId() == null || Objects.equals(row.ownerId(), safeFilter.ownerId()));
    }

    private String rowHashKey(String tag, String rowKey) {
        return rowHashKeyPrefix(tag) + rowKey;
    }

    private String rowHashKeyPrefix(String tag) {
        return prefix + ":row:" + tag + ":";
    }

    private String generationKey(String tag, String rowKey) {
        return prefix + ":gen:" + tag + ":" + rowKey;
    }

    private String kindIndexKey(String tag) {
        return prefix + ":keys:" + tag;
    }

    private String ownerIndexKeyPrefix(String tag) {
        return prefix + ":own:" + tag + ":";
    }

    private String leaseKey(String ownerId) {
        return leaseKeyPrefix() + ownerId;
    }

    private String leaseKeyPrefix() {
        return prefix + ":lease:";
    }

    private String leaseIndexKey() {
        return prefix + ":leases";
    }

    private String stampKey(String tag, String meshName) {
        return meshName == null ? prefix + ":stamp:" + tag : prefix + ":stamp:" + tag + ":" + meshName;
    }

    private static String tagOf(ZLinkLocationKind kind) {
        return switch (kind) {
            case PEER -> "peer";
            case SPOT -> "spot";
            case ACTOR -> "actor";
            case ROUTE -> "route";
            case INVALID -> throw new IllegalArgumentException("invalid location kind");
        };
    }

    @FunctionalInterface
    private interface RowDeserializer<T> {
        T deserialize(String json, long generation, Instant updatedAt);
    }
}
