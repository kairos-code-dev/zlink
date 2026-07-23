package systems.zlink.framework.locations.redis;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
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
import systems.zlink.framework.locations.ZLinkOwnerLeaseClaimed;
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
import systems.zlink.framework.locations.ZLinkRelocationDeleteResult;
import systems.zlink.framework.locations.ZLinkRelocationFound;
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

        store.claimOwnerLease("owner-a", Duration.ofSeconds(30))
            .toCompletableFuture().get();
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

        store.claimOwnerLease("owner-a", Duration.ofSeconds(30))
            .toCompletableFuture().get();
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

    @Test
    void redisRelocationStoreKeepsImmutableContentAddressedPayload() throws Exception {
        String endpoint = System.getenv("ZLINK_REDIS_LOCATION_ENDPOINT");
        assumeTrue(endpoint != null && !endpoint.isBlank(),
            "ZLINK_REDIS_LOCATION_ENDPOINT is not set");
        try (ZLinkRedisRelocationStore store = new ZLinkRedisRelocationStore(
            new ZLinkRedisRelocationOptions()
                .setConnectionString(endpoint)
                .setKeyPrefix("zlink:relocation-test:" + UUID.randomUUID()))) {
            byte[] payload = new byte[] {1, 2, 3, 4};
            var stored = store.put(
                    payload,
                    Duration.ofHours(24),
                    () -> false)
                .toCompletableFuture().get();
            payload[0] = 9;

            var found = assertInstanceOf(
                ZLinkRelocationFound.class,
                store.get(stored.reference(), () -> false)
                    .toCompletableFuture().get());
            assertArrayEquals(new byte[] {1, 2, 3, 4}, found.payload());
            assertTrue(stored.expiresAt().isAfter(stored.storeNow()));
            assertEquals(
                ZLinkRelocationDeleteResult.DELETED,
                store.delete(stored.reference(), () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                ZLinkRelocationDeleteResult.MISSING,
                store.delete(stored.reference(), () -> false)
                    .toCompletableFuture().get());
        }
    }

    @Test
    void redisAuthorityReservePreserveScanAndDeleteAreFenced() throws Exception {
        String endpoint = System.getenv("ZLINK_REDIS_LOCATION_ENDPOINT");
        assumeTrue(endpoint != null && !endpoint.isBlank(),
            "ZLINK_REDIS_LOCATION_ENDPOINT is not set");
        try (ZLinkRedisLocationStore store = new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions()
                .setConnectionString(endpoint)
                .setKeyPrefix("zlink:authority-test:" + UUID.randomUUID()))) {
            var owner = assertInstanceOf(
                ZLinkOwnerLeaseClaimed.class,
                store.claimOwnerLease("owner-a", Duration.ofSeconds(30))
                    .toCompletableFuture().get()).token();
            var request = new systems.zlink.framework.locations
                .ZLinkObjectReservationRequest(
                    systems.zlink.framework.locations.ZLinkPlacementObjectKind.ACTOR,
                    "zla1:a:4:game:7:actor-1",
                    "player",
                    Optional.empty(),
                    Optional.empty(),
                    "creation-root",
                    new byte[32],
                    32,
                    new systems.zlink.framework.locations.ZLinkMeshNodeDescriptorKey(
                        "game",
                        NODE_A),
                    1,
                    owner,
                    new byte[] {9, 8},
                    1);
            var reserved = assertInstanceOf(
                systems.zlink.framework.locations.ZLinkObjectReserved.class,
                store.reserve(request, () -> false).toCompletableFuture().get());
            var creating = assertInstanceOf(
                systems.zlink.framework.locations.ZLinkAuthoritySnapshot.class,
                store.read(request.authorityKey(), () -> false)
                    .toCompletableFuture().get());
            assertArrayEquals(new byte[] {9, 8}, creating.payload());
            assertEquals(owner.ownerId(), creating.ownerId());
            assertEquals(
                owner.leaseGeneration(),
                creating.ownerLeaseGeneration());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkPlacementAllocationState.PENDING,
                creating.allocation().state());
            assertEquals(
                request.targetDescriptor(),
                creating.allocation().descriptor());
            assertEquals(
                request.pendingCapacityDelta(),
                creating.allocation().capacityDelta());
            assertEquals(
                systems.zlink.framework.locations.ZLinkObjectCommitResult.COMMITTED,
                store.commit(
                        reserved.reservation(),
                        new byte[] {1, 2},
                        () -> false)
                    .toCompletableFuture().get());
            var current = assertInstanceOf(
                systems.zlink.framework.locations.ZLinkAuthoritySnapshot.class,
                store.read(request.authorityKey(), () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkPlacementAllocationState.ACTIVE,
                current.allocation().state());
            var updated = assertInstanceOf(
                systems.zlink.framework.locations.ZLinkAuthorityStored.class,
                store.compareExchange(
                        request.authorityKey(),
                        new systems.zlink.framework.locations.ZLinkAuthorityExpectFound(
                            current.storeVersion()),
                        new systems.zlink.framework.locations.ZLinkAuthorityPut(
                            new byte[] {3, 4},
                            systems.zlink.framework.locations
                                .ZLinkAuthorityGenerationTransition.PRESERVE,
                            Optional.empty(),
                            Optional.empty()),
                        () -> false)
                    .toCompletableFuture().get());
            assertArrayEquals(new byte[] {3, 4}, updated.payload());
            assertEquals(current.allocation(), updated.allocation());
            var page = assertInstanceOf(
                systems.zlink.framework.locations.ZLinkAuthorityPage.class,
                store.list("zla1:a:", Optional.empty(), 10, () -> false)
                    .toCompletableFuture().get());
            assertEquals(List.of(request.authorityKey()),
                page.items().stream()
                    .map(systems.zlink.framework.locations.ZLinkAuthorityEntry::key)
                    .toList());
            assertInstanceOf(
                systems.zlink.framework.locations.ZLinkAuthorityDeleted.class,
                store.compareExchange(
                        request.authorityKey(),
                        new systems.zlink.framework.locations.ZLinkAuthorityExpectFound(
                            updated.storeVersion()),
                        new systems.zlink.framework.locations.ZLinkAuthorityDelete(),
                        () -> false)
                    .toCompletableFuture().get());
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
