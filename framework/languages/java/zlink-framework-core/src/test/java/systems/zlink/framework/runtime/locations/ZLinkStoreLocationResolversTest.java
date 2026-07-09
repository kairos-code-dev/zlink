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
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.SpotRemoteRef;

class ZLinkStoreLocationResolversTest {
    private static final Instant NOW = Instant.parse("2026-07-03T00:00:00Z");
    private static final RoutingId NODE = RoutingId.from("node-a");
    private static final RoutingId SPOT = RoutingId.from("room-a");

    @Test
    void spotResolverReadsStoreAndRequiresLiveOwner() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        store.updateSpotAsync(spot("owner-a", 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        assertNull(resolvers(store).resolveSpotRowAsync(new ZLinkSpotLocationKey("game", SPOT))
            .toCompletableFuture()
            .get());

        store.renewOwnerLeaseAsync("owner-a", NODE, Duration.ofSeconds(30))
            .toCompletableFuture()
            .get();

        assertEquals(SPOT, resolvers(store).resolveSpotRowAsync(new ZLinkSpotLocationKey("game", SPOT))
            .toCompletableFuture()
            .get()
            .spotRid());
    }

    @Test
    void addressResolverDerivesActorEntryAndUserSpotAddresses() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ZLinkStoreLocationResolvers.AddressResolvers addresses =
            new ZLinkStoreLocationResolvers.AddressResolvers(List.of("game"), resolvers(store));
        store.renewOwnerLeaseAsync("owner-a", NODE, Duration.ofSeconds(30))
            .toCompletableFuture()
            .get();
        store.updateActorAsync(actor("owner-a", "entry-actor", ZLinkSpotKind.ENTRY, null), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        store.updateActorAsync(actor("owner-a", "user-actor", ZLinkSpotKind.USER, SPOT), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        SpotRef entry = addresses.resolveActorSpotRefAsync("entry-actor")
            .toCompletableFuture()
            .get();
        SpotRef user = addresses.resolveActorSpotRefAsync("user-actor")
            .toCompletableFuture()
            .get();

        assertEquals(NODE, entry.spotRid());
        assertEquals(SPOT, user.spotRid());
    }

    @Test
    void remoteAddressResolverUsesMappedRouterChannel() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ZLinkStoreLocationResolvers rows = resolvers(store);
        ZLinkStoreLocationResolvers.AddressResolvers addresses =
            new ZLinkStoreLocationResolvers.AddressResolvers(
                List.of("game"),
                Map.of("game", "play-route"),
                rows);
        store.renewOwnerLeaseAsync("owner-a", NODE, Duration.ofSeconds(30))
            .toCompletableFuture()
            .get();
        store.updateSpotAsync(spot("owner-a", 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        SpotRemoteRef address = new ZLinkLocationSpotRemoteRefResolver(addresses)
            .resolveSpotRemoteRefAsync(SPOT)
            .toCompletableFuture()
            .get();

        assertEquals("play-route", address.routerChannelId());
        assertEquals(NODE, address.targetNodeRid());
        assertEquals(SPOT, address.spotRid());
    }

    @Test
    void livePeerResolverDropsExpiredOwners() throws Exception {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore(Clock.fixed(NOW, ZoneOffset.UTC));
        ZLinkStoreLocationResolvers rows = resolvers(store);
        store.updatePeerAsync(peer("owner-a"), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        store.updateRouteAsync(route("owner-a"), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        assertEquals(List.of(), rows.listLivePeersAsync(ZLinkPeerLocationFilter.all()).toCompletableFuture().get());
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
            SPOT,
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
        RoutingId spotRid) {
        return new ZLinkActorLocation(
            actorId,
            "player",
            null,
            NODE,
            locationKind,
            "game",
            spotRid,
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
