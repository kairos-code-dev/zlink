package systems.zlink.framework.runtime.locations;

import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Flow;
import java.util.concurrent.atomic.AtomicBoolean;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationChanged;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationWatchFilter;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationResolver;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;
import systems.zlink.framework.spots.ZLinkSpotKind;

public final class ZLinkStoreLocationResolvers
    implements ZLinkPeerLocationResolver, AutoCloseable {
    private final ZLinkRegisteredLocationStores stores;
    private final ZLinkLiveLocationRows liveRows;
    private final Duration routeCacheMaxAge;
    private final Map<ZLinkSpotLocationKey, CachedRoute<ZLinkSpotLocation>> spotRoutes =
        new ConcurrentHashMap<>();
    private final Map<ZLinkActorLocationKey, CachedRoute<ZLinkActorLocation>> actorRoutes =
        new ConcurrentHashMap<>();
    private final ZLinkServiceAuthorityPayloadCodec spotAuthorityCodec =
        new ZLinkServiceAuthorityPayloadCodec();
    private final ZLinkActorAuthorityPayloadCodec actorAuthorityCodec =
        new ZLinkActorAuthorityPayloadCodec();
    private final CopyOnWriteArrayList<Flow.Subscription> watchSubscriptions =
        new CopyOnWriteArrayList<>();
    private final AtomicBoolean authorityStoreFailure = new AtomicBoolean();
    private volatile boolean closed;

    public ZLinkStoreLocationResolvers(
        ZLinkRegisteredLocationStores stores,
        ZLinkLocationOptions options) {
        this(stores, ZLinkLiveLocationRows.create(stores, options));
    }

    public ZLinkStoreLocationResolvers(
        ZLinkRegisteredLocationStores stores,
        ZLinkLiveLocationRows liveRows) {
        this.stores = Objects.requireNonNull(stores, "stores");
        this.liveRows = Objects.requireNonNull(liveRows, "liveRows");
        this.routeCacheMaxAge = liveRows.routeCacheMaxAge();
        subscribeToAuthorityChanges();
    }

    @Override
    public CompletionStage<List<ZLinkPeerLocation>> listLivePeers(ZLinkPeerLocationFilter filter) {
        return stores.peerStore().listPeerLocations(filter)
            .thenCompose(liveRows::filterLivePeers);
    }

    CompletionStage<ZLinkSpotLocation> resolveSpotRow(ZLinkSpotLocationKey key) {
        ZLinkSpotLocation cached = cached(spotRoutes, key);
        if (cached != null) {
            return CompletableFuture.completedFuture(cached);
        }
        if (stores.authorityStore() == null) {
            return liveRows.resolveLiveSpot(stores.spotStore().resolveSpot(key));
        }
        return observeAuthorityRead(stores.authorityStore()
            .read(ZLinkAuthorityKeyCodec.spot(key.spotId()), () -> false))
            .thenCompose(read -> resolveReadySpot(key, read));
    }

    public CompletionStage<ZLinkActorLocation> resolveActorRow(ZLinkActorLocationKey key) {
        ZLinkActorLocation cached = cached(actorRoutes, key);
        if (cached != null) {
            return CompletableFuture.completedFuture(cached);
        }
        if (stores.authorityStore() == null) {
            return CompletableFuture.completedFuture(null);
        }
        return observeAuthorityRead(stores.authorityStore()
            .read(ZLinkAuthorityKeyCodec.actor(key.actorId()), () -> false))
            .thenCompose(read -> resolveReadyActor(key, read));
    }

    public void invalidateActorRoute(ZLinkActorLocationKey key) {
        actorRoutes.remove(Objects.requireNonNull(key, "key"));
    }

    public void invalidateSpotRoute(ZLinkSpotLocationKey key) {
        spotRoutes.remove(Objects.requireNonNull(key, "key"));
    }

    public void invalidateAllRoutes() {
        spotRoutes.clear();
        actorRoutes.clear();
    }

    private CompletionStage<ZLinkSpotLocation> resolveReadySpot(
        ZLinkSpotLocationKey key,
        Object read) {
        if (!(read instanceof systems.zlink.framework.locations
                .ZLinkAuthoritySnapshot snapshot)
            || snapshot.allocation().state()
                != systems.zlink.framework.locations.ZLinkPlacementAllocationState.ACTIVE) {
            spotRoutes.remove(key);
            return CompletableFuture.completedFuture(null);
        }
        var authority = spotAuthorityCodec.decode(snapshot.payload()).orElse(null);
        if (authority == null
            || authority.state() != ZLinkServiceAuthorityPayloadCodec.State.READY
            || !authority.spotId().equals(key.spotId())
            || !authority.ownerId().equals(snapshot.ownerId())
            || authority.ownerLeaseGeneration() != snapshot.ownerLeaseGeneration()
            || snapshot.allocation().objectKind()
                != (authority.kind() == ZLinkServiceAuthorityPayloadCodec.Kind.USER
                    ? systems.zlink.framework.locations.ZLinkPlacementObjectKind.USER_SPOT
                    : systems.zlink.framework.locations.ZLinkPlacementObjectKind.INSTANCE_SPOT)) {
            spotRoutes.remove(key);
            return CompletableFuture.completedFuture(null);
        }
        ZLinkSpotLocation row = new ZLinkSpotLocation(
            authority.meshName(),
            authority.spotId(),
            snapshot.objectGeneration(),
            authority.stableType(),
            authority.nodeRid(),
            authority.kind() == ZLinkServiceAuthorityPayloadCodec.Kind.USER
                ? ZLinkSpotKind.USER
                : ZLinkSpotKind.INSTANCE,
            null,
            snapshot.ownerId(),
            snapshot.authorityOwnerGeneration(),
            snapshot.storeNow());
        return admitPositiveRoute(
            spotRoutes,
            key,
            row,
            snapshot.ownerId(),
            snapshot.ownerLeaseGeneration(),
            snapshot.storeVersion(),
            snapshot.allocation().descriptorLifecycleGeneration());
    }

    private CompletionStage<ZLinkActorLocation> resolveReadyActor(
        ZLinkActorLocationKey key,
        Object read) {
        if (!(read instanceof systems.zlink.framework.locations
                .ZLinkAuthoritySnapshot snapshot)
            || snapshot.allocation().state()
                != systems.zlink.framework.locations.ZLinkPlacementAllocationState.ACTIVE
            || snapshot.allocation().objectKind()
                != systems.zlink.framework.locations.ZLinkPlacementObjectKind.ACTOR) {
            actorRoutes.remove(key);
            return CompletableFuture.completedFuture(null);
        }
        var authority = actorAuthorityCodec.decode(snapshot.payload()).orElse(null);
        if (authority == null
            || authority.state() != ZLinkActorAuthorityPayloadCodec.State.READY
            || !authority.actorId().equals(key.actorId())
            || !authority.ownerId().equals(snapshot.ownerId())
            || authority.ownerLeaseGeneration() != snapshot.ownerLeaseGeneration()) {
            actorRoutes.remove(key);
            return CompletableFuture.completedFuture(null);
        }
        ZLinkActorLocation row = new ZLinkActorLocation(
            authority.actorId(),
            authority.stableType(),
            new systems.zlink.framework.actors.ActorRef(
                authority.actorId(),
                snapshot.objectGeneration(),
                authority.meshName(),
                authority.nodeRid()),
            authority.nodeRid(),
            authority.currentSpotKind() == 1
                ? ZLinkSpotKind.ENTRY
                : ZLinkSpotKind.USER,
            authority.meshName(),
            authority.currentSpotId(),
            snapshot.ownerId(),
            snapshot.authorityOwnerGeneration(),
            snapshot.storeNow());
        return admitPositiveRoute(
            actorRoutes,
            key,
            row,
            snapshot.ownerId(),
            snapshot.ownerLeaseGeneration(),
            snapshot.storeVersion(),
            snapshot.allocation().descriptorLifecycleGeneration());
    }

    private <K, V> CompletionStage<V> admitPositiveRoute(
        Map<K, CachedRoute<V>> cache,
        K key,
        V row,
        String ownerId,
        long ownerLeaseGeneration,
        String storeVersion,
        long nodeLifecycleGeneration) {
        return liveRows.ownerLeaseRemaining(ownerId, ownerLeaseGeneration)
            .thenApply(remaining -> {
                if (remaining == null) {
                    invalidateOwnerLease(ownerId, ownerLeaseGeneration);
                    return null;
                }
                if (!routeCacheMaxAge.isZero()) {
                    Duration lifetime = remaining.compareTo(routeCacheMaxAge) < 0
                        ? remaining
                        : routeCacheMaxAge;
                    cache.put(key, new CachedRoute<>(
                        row,
                        storeVersion,
                        ownerId,
                        ownerLeaseGeneration,
                        nodeLifecycleGeneration,
                        System.nanoTime(),
                        boundedNanos(lifetime)));
                }
                return row;
            });
    }

    private static <K, V> V cached(Map<K, CachedRoute<V>> cache, K key) {
        CachedRoute<V> route = cache.get(key);
        if (route == null) {
            return null;
        }
        if (System.nanoTime() - route.storedAtNanos() >= route.lifetimeNanos()) {
            cache.remove(key, route);
            return null;
        }
        return route.row();
    }

    private static long boundedNanos(Duration value) {
        try {
            return value.toNanos();
        } catch (ArithmeticException overflow) {
            return Long.MAX_VALUE;
        }
    }

    private <T> CompletionStage<T> observeAuthorityRead(
        CompletionStage<T> read) {
        return read.whenComplete((ignored, failure) -> {
            if (failure != null) {
                authorityStoreFailure.set(true);
            } else if (authorityStoreFailure.compareAndSet(true, false)) {
                invalidateAllRoutes();
            }
        });
    }

    private void invalidateOwnerLease(
        String ownerId,
        long ownerLeaseGeneration) {
        spotRoutes.entrySet().removeIf(entry ->
            entry.getValue().sameOwner(ownerId, ownerLeaseGeneration));
        actorRoutes.entrySet().removeIf(entry ->
            entry.getValue().sameOwner(ownerId, ownerLeaseGeneration));
    }

    private void subscribeToAuthorityChanges() {
        if (stores.watchStore() == null) {
            return;
        }
        subscribe(new ZLinkLocationWatchFilter(
            ZLinkLocationKind.SPOT,
            null,
            systems.zlink.framework.locations.ZLinkRouteKind.INVALID));
        subscribe(new ZLinkLocationWatchFilter(
            ZLinkLocationKind.ACTOR,
            null,
            systems.zlink.framework.locations.ZLinkRouteKind.INVALID));
    }

    private void subscribe(ZLinkLocationWatchFilter filter) {
        try {
            stores.watchStore().watch(filter).subscribe(new CacheSubscriber());
        } catch (RuntimeException failure) {
            authorityStoreFailure.set(true);
        }
    }

    @Override
    public void close() {
        closed = true;
        watchSubscriptions.forEach(Flow.Subscription::cancel);
        watchSubscriptions.clear();
        invalidateAllRoutes();
    }

    private record CachedRoute<V>(
        V row,
        String storeVersion,
        String ownerId,
        long ownerLeaseGeneration,
        long nodeLifecycleGeneration,
        long storedAtNanos,
        long lifetimeNanos) {
        private boolean sameOwner(
            String candidateOwnerId,
            long candidateLeaseGeneration) {
            return ownerId.equals(candidateOwnerId)
                && ownerLeaseGeneration == candidateLeaseGeneration;
        }
    }

    private final class CacheSubscriber
        implements Flow.Subscriber<ZLinkLocationChanged> {
        @Override
        public void onSubscribe(Flow.Subscription subscription) {
            if (closed) {
                subscription.cancel();
                return;
            }
            watchSubscriptions.add(subscription);
            subscription.request(Long.MAX_VALUE);
        }

        @Override
        public void onNext(ZLinkLocationChanged change) {
            if (change.key() instanceof systems.zlink.framework.locations
                    .ZLinkLocationKey.Spot spot) {
                invalidateSpotRoute(spot.key());
            } else if (change.key() instanceof systems.zlink.framework.locations
                    .ZLinkLocationKey.Actor actor) {
                invalidateActorRoute(actor.key());
            }
        }

        @Override
        public void onError(Throwable failure) {
            authorityStoreFailure.set(true);
            invalidateAllRoutes();
        }

        @Override
        public void onComplete() {
            invalidateAllRoutes();
        }
    }

    CompletionStage<ZLinkRouteLocation> resolveRouteRow(ZLinkRouteLocationKey key) {
        return liveRows.resolveLiveRoute(stores.routeStore().resolveRoute(key));
    }

    public static final class AddressResolvers {
        private final ZLinkStoreLocationResolvers rows;

        public AddressResolvers(
            List<String> meshNames,
            ZLinkStoreLocationResolvers rows) {
            Objects.requireNonNull(meshNames, "meshNames");
            this.rows = Objects.requireNonNull(rows, "rows");
        }

        public CompletionStage<ZLinkActorLocation> resolveActorSpotRow(String actorId) {
            return rows.resolveActorRow(new ZLinkActorLocationKey(actorId));
        }

        public CompletionStage<ZLinkSpotLocation> resolveSpotRow(
            String meshName,
            String spotId) {
            return rows.resolveSpotRow(new ZLinkSpotLocationKey(spotId));
        }

        public CompletionStage<ZLinkSpotLocation> resolveAnySpotRow(
            String spotId) {
            return rows.resolveSpotRow(new ZLinkSpotLocationKey(spotId));
        }

        public String routerChannelId(String meshName) {
            return meshName;
        }
    }
}
