package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.lang.reflect.Proxy;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.Flow;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationChangeType;
import systems.zlink.framework.locations.ZLinkLocationChanged;
import systems.zlink.framework.locations.ZLinkLocationKey;
import systems.zlink.framework.locations.ZLinkLocationWatchStore;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;
import systems.zlink.framework.spots.ZLinkSpotKind;

class ZLinkStoreLocationResolversTest {
    private static final Instant NOW = Instant.parse("2026-07-03T00:00:00Z");
    private static final RoutingId NODE = RoutingId.from("node-a");
    private static final RoutingId SPOT = RoutingId.from("room-a");

    @Test
    void canonicalAuthorityDoesNotFallBackToLegacySpotRow() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        store.updateSpot(spot("owner-a", 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        assertNull(resolvers(store).resolveSpotRow(new ZLinkSpotLocationKey(SPOT.toString()))
            .toCompletableFuture()
            .get());

        store.claimOwnerLease("owner-a", Duration.ofSeconds(30))
            .toCompletableFuture()
            .get();

        assertNull(resolvers(store).resolveSpotRow(
            new ZLinkSpotLocationKey(SPOT.toString()))
            .toCompletableFuture().get());
    }

    @Test
    void positiveReadyAuthorityRouteIsCachedButMissingIsNot() throws Exception {
        AtomicInteger authorityReads = new AtomicInteger();
        AtomicInteger leaseReads = new AtomicInteger();
        var codec = new ZLinkServiceAuthorityPayloadCodec();
        var current = new java.util.concurrent.atomic.AtomicReference<Object>(
            readySpotSnapshot(codec));
        systems.zlink.framework.locations.ZLinkLocationStore store =
            (systems.zlink.framework.locations.ZLinkLocationStore)
                Proxy.newProxyInstance(
                    getClass().getClassLoader(),
                    new Class<?>[] {
                        systems.zlink.framework.locations.ZLinkLocationStore.class
                    },
                    (proxy, method, arguments) -> switch (method.getName()) {
                        case "read" -> {
                            authorityReads.incrementAndGet();
                            yield CompletableFuture.completedFuture(current.get());
                        }
                        case "readOwnerLease" -> {
                            leaseReads.incrementAndGet();
                            yield CompletableFuture.completedFuture(
                                new systems.zlink.framework.locations.ZLinkOwnerLeaseFound(
                                    new systems.zlink.framework.locations.ZLinkLocationOwnerToken(
                                        "owner-a", 7),
                                    NOW.plusSeconds(30),
                                    NOW));
                        }
                        default -> throw new UnsupportedOperationException(method.getName());
                    });
        ZLinkLocationOptions options = new ZLinkLocationOptions();
        options.setRouteCacheMaxAge(Duration.ofSeconds(10));
        ZLinkStoreLocationResolvers rows = new ZLinkStoreLocationResolvers(
            ZLinkRegisteredLocationStores.fromUnified(store),
            options);
        ZLinkSpotLocationKey key = new ZLinkSpotLocationKey("room-a");

        assertEquals("room-a", rows.resolveSpotRow(key)
            .toCompletableFuture().get().spotId());
        assertEquals("room-a", rows.resolveSpotRow(key)
            .toCompletableFuture().get().spotId());
        assertEquals(1, authorityReads.get());
        assertEquals(1, leaseReads.get());

        ZLinkLocationOptions noCache = new ZLinkLocationOptions();
        noCache.setRouteCacheMaxAge(Duration.ZERO);
        current.set(new systems.zlink.framework.locations.ZLinkAuthorityMissing(NOW));
        ZLinkStoreLocationResolvers misses = new ZLinkStoreLocationResolvers(
            ZLinkRegisteredLocationStores.fromUnified(store),
            noCache);
        assertNull(misses.resolveSpotRow(key).toCompletableFuture().get());
        assertNull(misses.resolveSpotRow(key).toCompletableFuture().get());
        assertEquals(3, authorityReads.get());
    }

    @Test
    void authorityWatchInvalidatesOnlyTheChangedReadyRoute() throws Exception {
        AtomicInteger authorityReads = new AtomicInteger();
        ManualPublisher changes = new ManualPublisher();
        var codec = new ZLinkServiceAuthorityPayloadCodec();
        systems.zlink.framework.locations.ZLinkLocationStore store =
            (systems.zlink.framework.locations.ZLinkLocationStore)
                Proxy.newProxyInstance(
                    getClass().getClassLoader(),
                    new Class<?>[] {
                        systems.zlink.framework.locations.ZLinkLocationStore.class,
                        ZLinkLocationWatchStore.class
                    },
                    (proxy, method, arguments) -> switch (method.getName()) {
                        case "watch" -> changes;
                        case "read" -> {
                            authorityReads.incrementAndGet();
                            yield CompletableFuture.completedFuture(
                                readySpotSnapshot(codec));
                        }
                        case "readOwnerLease" -> CompletableFuture.completedFuture(
                            new systems.zlink.framework.locations.ZLinkOwnerLeaseFound(
                                new systems.zlink.framework.locations.ZLinkLocationOwnerToken(
                                    "owner-a", 7),
                                NOW.plusSeconds(30),
                                NOW));
                        default -> throw new UnsupportedOperationException(method.getName());
                    });
        ZLinkStoreLocationResolvers rows = new ZLinkStoreLocationResolvers(
            ZLinkRegisteredLocationStores.fromUnified(store),
            new ZLinkLocationOptions());
        ZLinkSpotLocationKey key = new ZLinkSpotLocationKey("room-a");

        assertEquals("room-a", rows.resolveSpotRow(key)
            .toCompletableFuture().get().spotId());
        assertEquals("room-a", rows.resolveSpotRow(key)
            .toCompletableFuture().get().spotId());
        assertEquals(1, authorityReads.get());

        changes.publish(new ZLinkLocationChanged(
            systems.zlink.framework.locations.ZLinkLocationKind.SPOT,
            new ZLinkLocationKey.Spot(key),
            ZLinkLocationChangeType.UPSERTED,
            14,
            NOW));

        assertEquals("room-a", rows.resolveSpotRow(key)
            .toCompletableFuture().get().spotId());
        assertEquals(2, authorityReads.get());
        rows.close();
    }

    @Test
    void firstSuccessfulAuthorityReadAfterFailureInvalidatesReadyCache()
        throws Exception {
        AtomicInteger authorityReads = new AtomicInteger();
        AtomicBoolean failNext = new AtomicBoolean();
        var codec = new ZLinkServiceAuthorityPayloadCodec();
        systems.zlink.framework.locations.ZLinkLocationStore store =
            (systems.zlink.framework.locations.ZLinkLocationStore)
                Proxy.newProxyInstance(
                    getClass().getClassLoader(),
                    new Class<?>[] {
                        systems.zlink.framework.locations.ZLinkLocationStore.class
                    },
                    (proxy, method, arguments) -> switch (method.getName()) {
                        case "read" -> {
                            authorityReads.incrementAndGet();
                            if (failNext.compareAndSet(true, false)) {
                                yield CompletableFuture.failedFuture(
                                    new IllegalStateException("store unavailable"));
                            }
                            yield CompletableFuture.completedFuture(
                                readySpotSnapshot(codec));
                        }
                        case "readOwnerLease" -> CompletableFuture.completedFuture(
                            new systems.zlink.framework.locations.ZLinkOwnerLeaseFound(
                                new systems.zlink.framework.locations.ZLinkLocationOwnerToken(
                                    "owner-a", 7),
                                NOW.plusSeconds(30),
                                NOW));
                        default -> throw new UnsupportedOperationException(method.getName());
                    });
        ZLinkStoreLocationResolvers rows = new ZLinkStoreLocationResolvers(
            ZLinkRegisteredLocationStores.fromUnified(store),
            new ZLinkLocationOptions());
        ZLinkSpotLocationKey ready = new ZLinkSpotLocationKey("room-a");
        ZLinkSpotLocationKey other = new ZLinkSpotLocationKey("room-b");

        assertEquals("room-a", rows.resolveSpotRow(ready)
            .toCompletableFuture().get().spotId());
        failNext.set(true);
        assertThrows(CompletionException.class, () -> rows.resolveSpotRow(other)
            .toCompletableFuture().join());
        assertNull(rows.resolveSpotRow(other).toCompletableFuture().get());
        assertEquals("room-a", rows.resolveSpotRow(ready)
            .toCompletableFuture().get().spotId());
        assertEquals(4, authorityReads.get());
    }

    @Test
    void livePeerResolverDropsExpiredOwners() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ZLinkStoreLocationResolvers rows = resolvers(store);
        store.updatePeer(peer("owner-a"), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        store.updateRoute(route("owner-a"), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        assertEquals(List.of(), rows.listLivePeers(ZLinkPeerLocationFilter.all()).toCompletableFuture().get());
    }

    @Test
    void queryAndResolverShareObservedGenerationGuard() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ZLinkRegisteredLocationStores stores = ZLinkRegisteredLocationStores.fromUnified(store);
        ZLinkLocationOptions options = new ZLinkLocationOptions();
        options.setPollingInterval(Duration.ofMillis(1));
        ZLinkLiveLocationRows liveRows = ZLinkLiveLocationRows.create(stores, options);
        store.claimOwnerLease("owner-a", Duration.ofSeconds(30))
            .toCompletableFuture()
            .get();
        store.updateSpot(spot("owner-a", 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        assertEquals(1, liveRows.filterLiveSpots(List.of(spot("owner-a", 5)))
            .toCompletableFuture()
            .get()
            .size());
        ZLinkStoreLocationResolvers rows = new ZLinkStoreLocationResolvers(stores, liveRows);

        assertNull(rows.resolveSpotRow(new ZLinkSpotLocationKey(SPOT.toString()))
            .toCompletableFuture()
            .get());
    }

    @Test
    void actorResolverTreatsPendingRowWithoutActorRefAsMiss() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        store.claimOwnerLease("owner-a", Duration.ofSeconds(30))
            .toCompletableFuture()
            .get();
        store.updateActor(
                actor("owner-a", "player-pending", ZLinkSpotKind.ENTRY, null),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        assertNull(resolvers(store).resolveActorRow(new ZLinkActorLocationKey("player-pending"))
            .toCompletableFuture()
            .get());
    }

    private static ZLinkStoreLocationResolvers resolvers(ZLinkInMemoryLocationStore store) {
        ZLinkLocationOptions options = new ZLinkLocationOptions();
        options.setPollingInterval(Duration.ofMillis(1));
        return new ZLinkStoreLocationResolvers(
            ZLinkRegisteredLocationStores.fromUnified(store),
            options);
    }

    private static systems.zlink.framework.locations.ZLinkAuthoritySnapshot
        readySpotSnapshot(ZLinkServiceAuthorityPayloadCodec codec) {
        return new systems.zlink.framework.locations.ZLinkAuthoritySnapshot(
            "v1",
            codec.encodeUser(
                ZLinkServiceAuthorityPayloadCodec.State.READY,
                "RoomSpot",
                "room-a",
                "owner-a",
                7,
                "game",
                NODE,
                11),
            5,
            13,
            "owner-a",
            7,
            new systems.zlink.framework.locations.ZLinkPlacementAllocation(
                systems.zlink.framework.locations.ZLinkPlacementAllocationState.ACTIVE,
                systems.zlink.framework.locations.ZLinkPlacementObjectKind.USER_SPOT,
                "RoomSpot",
                new systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey(
                    "game", NODE),
                11,
                systems.zlink.framework.locations.ZLinkPlacementCapacityBundle.spot(
                    systems.zlink.framework.locations.ZLinkPlacementObjectKind.USER_SPOT,
                    "RoomSpot",
                    1)),
            Optional.empty(),
            NOW);
    }

    private static ZLinkSpotLocation spot(String ownerId, long generation) {
        return new ZLinkSpotLocation(
            "game",
            SPOT.toString(),
            "RoomSpot",
            NODE,
            ZLinkSpotKind.USER,
            "tcp://127.0.0.1:6000",
            ownerId,
            generation,
            NOW);
    }

    private static ZLinkActorLocation actor(
        String ownerId,
        String actorId,
        ZLinkSpotKind locationKind,
        String spotId) {
        return new ZLinkActorLocation(
            actorId,
            "player",
            null,
            NODE,
            locationKind,
            "game",
            spotId,
            ownerId,
            0,
            NOW);
    }

    private static ZLinkPeerLocation peer(String ownerId) {
        return new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "route",
            NODE,
            ZLinkLocationRole.ROUTER,
            "tcp://127.0.0.1:6000",
            1,
            false,
            0,
            Map.of(),
            List.of(),
            ownerId,
            0,
            NOW);
    }

    private static ZLinkRouteLocation route(String ownerId) {
        return new ZLinkRouteLocation(
            ZLinkRouteKind.ACTOR_SESSION,
            "session",
            NODE,
            ownerId,
            0,
            "actor".getBytes(java.nio.charset.StandardCharsets.UTF_8),
            NOW);
    }

    private static final class ManualPublisher
        implements Flow.Publisher<ZLinkLocationChanged> {
        private final List<Flow.Subscriber<? super ZLinkLocationChanged>>
            subscribers = new java.util.concurrent.CopyOnWriteArrayList<>();

        @Override
        public void subscribe(
            Flow.Subscriber<? super ZLinkLocationChanged> subscriber) {
            subscribers.add(subscriber);
            subscriber.onSubscribe(new Flow.Subscription() {
                @Override public void request(long count) {
                }

                @Override public void cancel() {
                    subscribers.remove(subscriber);
                }
            });
        }

        void publish(ZLinkLocationChanged change) {
            subscribers.forEach(subscriber -> subscriber.onNext(change));
        }
    }
}
