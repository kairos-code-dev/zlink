package systems.zlink.framework.locations;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.runtime.locations.ZLinkLocationRuntime;
import systems.zlink.framework.runtime.locations.ZLinkLocationRuntimeQueryService;
import systems.zlink.framework.runtime.locations.ZLinkRegisteredLocationStores;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.spots.ZLinkStoreSpotHandleResolver;

final class LocationStoreContractTest {
    private static final Instant STORE_NOW = Instant.parse("2026-07-03T00:00:00Z");
    private static final RoutingId NODE_A = RoutingId.from("node-a");
    private static final RoutingId NODE_B = RoutingId.from("node-b");
    private static final RoutingId SPOT_RID = RoutingId.from("spot-1");

    @Test
    void storeIssuesGenerationsAndGuardsWritesWithOwnerTokens() throws Exception {
        ZLinkInMemoryLocationStore store = newStore();
        store.claimOwnerLease("owner-a", Duration.ofSeconds(30))
            .toCompletableFuture()
            .get();
        ZLinkOwnerLeaseClaimed ownerB =
            (ZLinkOwnerLeaseClaimed) store.claimOwnerLease(
                    "owner-b",
                    Duration.ofSeconds(30))
                .toCompletableFuture()
                .get();

        ZLinkLocationWriteResult claimed = store.updateActor(
                actor("owner-a", 0),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        ZLinkLocationWriteResult conflict = store.updateActor(
                actor("owner-b", 0),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        ZLinkLocationWriteResult renewed = store.updateActor(
                actor("owner-a", claimed.generation()),
                ZLinkLocationWriteIntent.RENEW)
            .toCompletableFuture()
            .get();
        ZLinkLocationWriteResult takeover = store.updateActor(
                actor("owner-b", 0),
                ZLinkLocationWriteIntent.TAKEOVER)
            .toCompletableFuture()
            .get();
        ZLinkLocationWriteResult stale = store.updateActor(
                actor("owner-a", claimed.generation()),
                ZLinkLocationWriteIntent.RENEW)
            .toCompletableFuture()
            .get();

        assertEquals(ZLinkLocationWriteStatus.STORED, claimed.status());
        assertEquals(1, claimed.generation());
        assertEquals(ZLinkLocationWriteStatus.REJECTED_CONFLICT, conflict.status());
        assertEquals(ZLinkLocationWriteStatus.STORED, renewed.status());
        assertEquals(claimed.generation(), renewed.generation());
        assertEquals(ZLinkLocationWriteStatus.STORED, takeover.status());
        assertEquals(2, takeover.generation());
        assertEquals(ZLinkLocationWriteStatus.IGNORED_STALE, stale.status());

        assertEquals(
            1,
            store.removeAllByOwner(ownerB.token())
                .toCompletableFuture()
                .get());
    }

    @Test
    void unifiedStoreExposesAllRoleStoresAndPagedLists() throws Exception {
        ZLinkInMemoryLocationStore store = newStore();
        ZLinkRegisteredLocationStores stores = ZLinkRegisteredLocationStores.fromUnified(store);

        assertSame(store, stores.peerStore());
        assertSame(store, stores.spotStore());
        assertSame(store, stores.actorStore());
        assertSame(store, stores.routeStore());
        assertSame(store, stores.ownerLeaseStore());
        assertNotNull(stores.changeStampStore());

        store.updatePeer(peer("owner-a", NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        store.updateSpot(spot("owner-a", 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        store.updateRoute(route("owner-a", 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        assertEquals(1, store.listPeerLocations(new ZLinkPeerLocationFilter(
                ZLinkLocationAutoConnectType.ROUTE_MESH,
                "play",
                ZLinkLocationRole.ROUTER,
                null,
                null))
            .toCompletableFuture()
            .get()
            .size());
        assertEquals(SPOT_RID, store.resolveSpot(new ZLinkSpotLocationKey("play", SPOT_RID))
            .toCompletableFuture()
            .get()
            .spotRid());

        ZLinkLocationPage<ZLinkRouteLocation> routePage = store.listRouteLocations(
                new ZLinkRouteLocationFilter(ZLinkRouteKind.ACTOR_SESSION, NODE_A, null),
                new ZLinkPageRequest(1, null))
            .toCompletableFuture()
            .get();

        assertEquals(1, routePage.items().size());
        assertNull(routePage.continuationToken());
        assertArrayEquals(new byte[] {1, 2, 3}, routePage.items().get(0).value());
    }

    @Test
    void resolversReadStoreAndFilterRowsWithoutLiveOwnerLease() throws Exception {
        ZLinkInMemoryLocationStore store = newStore();
        ZLinkRegisteredLocationStores stores = ZLinkRegisteredLocationStores.fromUnified(store);
        ZLinkStoreLocationResolvers rows =
            new ZLinkStoreLocationResolvers(stores, new ZLinkLocationOptions());
        ZLinkStoreLocationResolvers.AddressResolvers addresses =
            new ZLinkStoreLocationResolvers.AddressResolvers(List.of("play"), rows);

        store.updatePeer(peer("expired-owner", NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        store.updateSpot(spot("expired-owner", 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();

        assertEquals(List.of(), rows.listLivePeers(ZLinkPeerLocationFilter.all())
            .toCompletableFuture()
            .get());
        assertTrue(new ZLinkStoreSpotHandleResolver(addresses)
            .resolveSpotHandle(SPOT_RID)
            .toCompletableFuture()
            .get().isEmpty());

        store.claimOwnerLease("owner-a", Duration.ofSeconds(30))
            .toCompletableFuture()
            .get();
        store.updatePeer(peer("owner-a", NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        store.updateSpot(spot("owner-a", 0), ZLinkLocationWriteIntent.TAKEOVER)
            .toCompletableFuture()
            .get();

        rows = new ZLinkStoreLocationResolvers(stores, new ZLinkLocationOptions());
        addresses = new ZLinkStoreLocationResolvers.AddressResolvers(List.of("play"), rows);

        assertEquals(1, rows.listLivePeers(ZLinkPeerLocationFilter.all())
            .toCompletableFuture()
            .get()
            .size());
        assertEquals(SPOT_RID,
            new ZLinkStoreSpotHandleResolver(addresses).resolveSpotHandle(SPOT_RID)
                .toCompletableFuture()
                .get().orElseThrow().spotRid());
    }

    @Test
    void runtimeQueryReadsStoreDirectlyAndOmitsStaleRows() throws Exception {
        ZLinkInMemoryLocationStore store = newStore();
        ZLinkRegisteredLocationStores stores = ZLinkRegisteredLocationStores.fromUnified(store);
        ZLinkLocationRuntime runtime =
            new ZLinkLocationRuntime(stores, Duration.ofSeconds(30), Duration.ofSeconds(5));
        runtime.start(NODE_A).toCompletableFuture().get();
        try (runtime) {
            store.updatePeer(peer(runtime.ownerId(), NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
                .toCompletableFuture()
                .get();
            store.updatePeer(peer("expired-owner", NODE_B, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
                .toCompletableFuture()
                .get();

            ZLinkLocationRuntimeQueryService query = new ZLinkLocationRuntimeQueryService(
                stores,
                runtime,
                new ZLinkLocationOptions());

            ZLinkLocationRuntimeStatus status = query.getStatus()
                .toCompletableFuture()
                .get();
            List<ZLinkPeerLocation> peers = query.listPeerLocations(ZLinkPeerLocationFilter.all())
                .toCompletableFuture()
                .get();
            ZLinkLocationPage<ZLinkLocationTopologyEntry> topology = query.listTopology(
                    ZLinkLocationTopologyFilter.all(),
                    ZLinkPageRequest.firstPage())
                .toCompletableFuture()
                .get();
            List<ZLinkLocationServiceSummary> summaries = query.listServiceSummaries(
                    ZLinkLocationServiceSummaryFilter.all())
                .toCompletableFuture()
                .get();

            assertTrue(status.storeHealthy());
            assertTrue(status.ownerLeaseHealthy());
            assertFalse(status.watchEnabled());
            assertEquals(1, peers.size());
            assertEquals(NODE_A, peers.get(0).nodeRid());
            assertEquals(1, topology.items().size());
            assertEquals(1, summaries.size());
            assertEquals(1, summaries.get(0).readyCount());
        }
    }

    @Test
    void runtimeQueryUsesConfiguredPageSizeForImplicitFirstPage() throws Exception {
        ZLinkInMemoryLocationStore store = newStore();
        ZLinkRegisteredLocationStores stores = ZLinkRegisteredLocationStores.fromUnified(store);
        ZLinkLocationRuntime runtime =
            new ZLinkLocationRuntime(stores, Duration.ofSeconds(30), Duration.ofSeconds(5));
        runtime.start(NODE_A).toCompletableFuture().get();
        try (runtime) {
            for (int index = 1; index <= 3; index++) {
                store.updateSpot(
                        spot(runtime.ownerId(), 0, RoutingId.from("spot-" + index)),
                        ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture()
                    .get();
            }
            ZLinkLocationOptions options = new ZLinkLocationOptions();
            options.setListPageSize(2);
            ZLinkLocationRuntimeQueryService query = new ZLinkLocationRuntimeQueryService(
                stores,
                runtime,
                options);

            ZLinkLocationPage<ZLinkSpotLocation> page = query.listSpotLocations(
                    ZLinkSpotLocationFilter.all(),
                    ZLinkPageRequest.firstPage())
                .toCompletableFuture()
                .get();

            assertEquals(2, page.items().size());
            assertNotNull(page.continuationToken());

            ZLinkLocationPage<ZLinkLocationTopologyEntry> topologyFirst = query.listTopology(
                    ZLinkLocationTopologyFilter.all(),
                    ZLinkPageRequest.firstPage())
                .toCompletableFuture()
                .get();
            assertEquals(2, topologyFirst.items().size());
            assertNotNull(topologyFirst.continuationToken());
            ZLinkLocationPage<ZLinkLocationTopologyEntry> topologySecond = query.listTopology(
                    ZLinkLocationTopologyFilter.all(),
                    new ZLinkPageRequest(0, topologyFirst.continuationToken()))
                .toCompletableFuture()
                .get();
            assertEquals(1, topologySecond.items().size());
            assertNull(topologySecond.continuationToken());
        }
    }

    private static ZLinkInMemoryLocationStore newStore() {
        return new ZLinkInMemoryLocationStore(Clock.fixed(STORE_NOW, ZoneOffset.UTC));
    }

    private static ZLinkPeerLocation peer(String ownerId, RoutingId nodeRid, long generation) {
        return new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "play",
            nodeRid,
            ZLinkLocationRole.ROUTER,
            "tcp://127.0.0.1:5001",
            100,
            false,
            0,
            Map.of("zone", "a"),
            List.of("actor-route"),
            ownerId,
            generation,
            STORE_NOW);
    }

    private static ZLinkSpotLocation spot(String ownerId, long generation) {
        return spot(ownerId, generation, SPOT_RID);
    }

    private static ZLinkSpotLocation spot(
        String ownerId,
        long generation,
        RoutingId spotRid) {
        return new ZLinkSpotLocation(
            "play",
            spotRid,
            1,
            "game",
            NODE_A,
            ZLinkSpotKind.USER,
            "tcp://127.0.0.1:5001",
            ownerId,
            generation,
            STORE_NOW);
    }

    private static ZLinkActorLocation actor(String ownerId, long generation) {
        return new ZLinkActorLocation(
            "actor-1",
            "player",
            null,
            NODE_A,
            ZLinkSpotKind.ENTRY,
            "",
            null,
            ownerId,
            generation,
            STORE_NOW);
    }

    private static ZLinkRouteLocation route(String ownerId, long generation) {
        return new ZLinkRouteLocation(
            ZLinkRouteKind.ACTOR_SESSION,
            "session-1",
            NODE_A,
            ownerId,
            generation,
            new byte[] {1, 2, 3},
            STORE_NOW);
    }
}
