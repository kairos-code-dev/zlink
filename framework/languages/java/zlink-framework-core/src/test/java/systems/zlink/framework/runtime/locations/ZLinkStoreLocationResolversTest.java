package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationOptions;
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
    void spotResolverReadsStoreAndRequiresLiveOwner() throws Exception {
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

        assertEquals(SPOT.toString(), resolvers(store).resolveSpotRow(new ZLinkSpotLocationKey(SPOT.toString()))
            .toCompletableFuture()
            .get()
            .spotId());
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
}
