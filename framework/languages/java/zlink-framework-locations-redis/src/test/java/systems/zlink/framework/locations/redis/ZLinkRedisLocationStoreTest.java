package systems.zlink.framework.locations.redis;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationChangeStampScope;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationOwnerToken;
import systems.zlink.framework.locations.ZLinkPageRequest;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.locations.ZLinkLocationWriteStatus;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkPeerLocationFilter;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.locations.ZLinkSpotLocationFilter;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAcquireRequest;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAcquired;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotAllocationMember;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotGroupExhausted;
import systems.zlink.framework.locations.ZLinkRoutingIdSlotReleaseResult;
import systems.zlink.framework.spots.ZLinkSpotKind;

class ZLinkRedisLocationStoreTest {
    private static final Instant UPDATED_AT = Instant.parse("2026-07-03T00:00:00Z");
    private static final RoutingId NODE_A = RoutingId.from(new byte[] {0x01});

    @Test
    void redisStoreUsesLeaseAwareClaimGenerationAndOwnerGuardedRemove() throws Exception {
        String endpoint = System.getenv("ZLINK_REDIS_LOCATION_ENDPOINT");
        assumeTrue(endpoint != null && !endpoint.isBlank(), "ZLINK_REDIS_LOCATION_ENDPOINT is not set");

        ZLinkRedisLocationStore store = new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(endpoint)
            .setKeyPrefix("zlink:test:" + UUID.randomUUID()));

        store.renewOwnerLease("owner-a", NODE_A, Duration.ofSeconds(30)).toCompletableFuture().get();
        var first = store.updatePeer(peer("owner-a", NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        var conflict = store.updatePeer(peer("owner-b", NODE_A, 0), ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .get();
        var staleRemove = store.removePeer(peerKey(NODE_A), new ZLinkLocationOwnerToken("owner-b", first.generation()))
            .toCompletableFuture()
            .get();
        var remove = store.removePeer(peerKey(NODE_A), new ZLinkLocationOwnerToken("owner-a", first.generation()))
            .toCompletableFuture()
            .get();

        assertEquals(ZLinkLocationWriteStatus.STORED, first.status());
        assertEquals(1, first.generation());
        assertEquals(ZLinkLocationWriteStatus.REJECTED_CONFLICT, conflict.status());
        assertEquals(ZLinkLocationWriteStatus.IGNORED_STALE, staleRemove.status());
        assertEquals(ZLinkLocationWriteStatus.STORED, remove.status());
        assertEquals(List.of(), store.listPeerLocations(ZLinkPeerLocationFilter.all()).toCompletableFuture().get());
        assertEquals(2, store.getChangeStamp(new ZLinkLocationChangeStampScope(ZLinkLocationKind.PEER, "mesh"))
            .toCompletableFuture()
            .get());

        store.close();
    }

    @Test
    void redisStoreUsesScanCursorForPagedLocationLists() throws Exception {
        String endpoint = System.getenv("ZLINK_REDIS_LOCATION_ENDPOINT");
        assumeTrue(endpoint != null && !endpoint.isBlank(), "ZLINK_REDIS_LOCATION_ENDPOINT is not set");

        ZLinkRedisLocationStore store = new ZLinkRedisLocationStore(new ZLinkRedisLocationOptions()
            .setConnectionString(endpoint)
            .setKeyPrefix("zlink:test:" + UUID.randomUUID()));

        store.renewOwnerLease("owner-a", NODE_A, Duration.ofSeconds(30)).toCompletableFuture().get();
        store.updateSpot(spot("alpha"), ZLinkLocationWriteIntent.NEW_CLAIM).toCompletableFuture().get();
        store.updateSpot(spot("beta"), ZLinkLocationWriteIntent.NEW_CLAIM).toCompletableFuture().get();

        List<String> spotTypes = new java.util.ArrayList<>();
        String token = null;
        for (int attempt = 0; attempt < 8; attempt++) {
            var page = store.listSpotLocations(
                    ZLinkSpotLocationFilter.all(),
                    new ZLinkPageRequest(1, token))
                .toCompletableFuture()
                .get();
            page.items().stream().map(ZLinkSpotLocation::spotType).forEach(spotTypes::add);
            token = page.continuationToken();
            if (token == null) {
                break;
            }
        }

        assertEquals(List.of("alpha", "beta"), spotTypes.stream().sorted().toList());

        store.close();
    }

    @Test
    void redisRoutingIdSlotsAreAtomicIdempotentAndFenced() throws Exception {
        String endpoint = System.getenv("ZLINK_REDIS_LOCATION_ENDPOINT");
        assumeTrue(endpoint != null && !endpoint.isBlank(),
            "ZLINK_REDIS_LOCATION_ENDPOINT is not set");
        try (ZLinkRedisLocationStore store = new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions()
                .setConnectionString(endpoint)
                .setKeyPrefix("zlink:test:" + UUID.randomUUID()))) {
            List<ZLinkRoutingIdSlotAllocationMember> members = List.of(
                new ZLinkRoutingIdSlotAllocationMember("mesh", "node"));
            var first = assertInstanceOf(ZLinkRoutingIdSlotAcquired.class,
                store.acquireRoutingIdSlot(new ZLinkRoutingIdSlotAcquireRequest(
                    "group", members, 1, "owner-a", Duration.ofSeconds(30)))
                    .toCompletableFuture().get());
            var retried = assertInstanceOf(ZLinkRoutingIdSlotAcquired.class,
                store.acquireRoutingIdSlot(new ZLinkRoutingIdSlotAcquireRequest(
                    "group", members, 1, "owner-a", Duration.ofSeconds(30)))
                    .toCompletableFuture().get());
            assertInstanceOf(ZLinkRoutingIdSlotGroupExhausted.class,
                store.acquireRoutingIdSlot(new ZLinkRoutingIdSlotAcquireRequest(
                    "group", members, 1, "owner-b", Duration.ofSeconds(30)))
                    .toCompletableFuture().get());

            assertEquals(first.allocation().owner(), retried.allocation().owner());
            assertEquals(ZLinkRoutingIdSlotReleaseResult.RELEASED,
                store.releaseRoutingIdSlot("group", 1, first.allocation().owner())
                    .toCompletableFuture().get());
            assertEquals(ZLinkRoutingIdSlotReleaseResult.IGNORED_STALE,
                store.releaseRoutingIdSlot("group", 1, first.allocation().owner())
                    .toCompletableFuture().get());
            assertEquals(List.of(), store.listRoutingIdSlots("group")
                .toCompletableFuture().get().allocations());
        }
    }

    private static ZLinkPeerLocation peer(String ownerId, RoutingId nodeRid, long generation) {
        return new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "mesh",
            nodeRid,
            ZLinkLocationRole.ROUTER,
            "tcp://127.0.0.1:6000",
            1,
            false,
            0,
            Map.of(),
            List.of(),
            ownerId,
            generation,
            UPDATED_AT);
    }

    private static ZLinkPeerLocationKey peerKey(RoutingId nodeRid) {
        return new ZLinkPeerLocationKey(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "mesh",
            ZLinkLocationRole.ROUTER,
            nodeRid,
            null);
    }

    private static ZLinkSpotLocation spot(String spotType) {
        return new ZLinkSpotLocation(
            "mesh",
            RoutingId.fromHex(spotType.equals("alpha") ? "aa" : "bb"),
            spotType,
            NODE_A,
            ZLinkSpotKind.USER,
            "tcp://127.0.0.1:6000",
            "owner-a",
            0,
            UPDATED_AT);
    }
}
