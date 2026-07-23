package systems.zlink.framework.locations.redis;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;
import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

import io.lettuce.core.RedisClient;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
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
    private static final ObjectMapper JSON = new ObjectMapper();
    private static final Instant UPDATED_AT = Instant.parse("2026-07-03T00:00:00Z");
    private static final RoutingId NODE_A = RoutingId.from(new byte[] {0x01});

    @Test
    void authorityPhysicalEncodingMatchesSharedFixture() throws Exception {
        JsonNode fixture = authorityFixture();
        JsonNode buckets = fixture.path("capacityBuckets");
        var descriptor = new systems.zlink.framework.locations
            .ZLinkMeshNodeDescriptorKey(
                fixture.path("keyContract").path("meshName").asText(),
                RoutingId.fromHex("67616d652d61"));
        String descriptorKey =
            ZLinkRedisLocationKeyCodec.encodeMeshNodeKey(descriptor);

        assertEquals(
            buckets.path("descriptorKey").asText(),
            descriptorKey);
        assertEquals(
            buckets.path("node").asText(),
            capacitySegment(descriptorKey)
                + capacitySegment(
                    buckets.path(
                        "descriptorLifecycleGeneration").asText()));
        assertEquals(
            buckets.path("type").asText(),
            capacityTypeBucket(
                descriptorKey,
                buckets.path(
                    "descriptorLifecycleGeneration").asText(),
                buckets.path("objectKind").asText(),
                buckets.path("stableType").asText()));
        assertEquals(
            buckets.path("unicodeType").asText(),
            capacityTypeBucket(
                descriptorKey,
                buckets.path(
                    "descriptorLifecycleGeneration").asText(),
                buckets.path("objectKind").asText(),
                buckets.path("unicodeStableType").asText()));
        assertEquals(
            List.of(
                "authorityKey",
                "payload",
                "storeVersion",
                "objectGeneration",
                "authorityOwnerGeneration",
                "ownerId",
                "ownerLeaseGeneration",
                "allocationState",
                "objectKind",
                "stableType",
                "descriptorKey",
                "descriptorLifecycleGeneration",
                "capacityDelta"),
            JSON.convertValue(
                fixture.path("currentHashFields"),
                JSON.getTypeFactory().constructCollectionType(
                    List.class,
                    String.class)));
    }

    @Test
    void meshNodeDescriptorPhysicalEncodingMatchesSharedFixture()
        throws Exception {
        JsonNode fixture = descriptorFixture();
        JsonNode physical = fixture.path("physicalKeys");
        JsonNode row = fixture.path("row");
        String canonicalKey = row.path("key").asText();
        var keys = new ZLinkRedisLocationKeys("P");
        var descriptorKey =
            new systems.zlink.framework.locations
                .ZLinkMeshNodeDescriptorKey(
                    row.path("hash").path("mesh").asText(),
                    RoutingId.fromHex("67616d652d61"));

        assertEquals(
            canonicalKey,
            ZLinkRedisLocationKeyCodec.encodeMeshNodeKey(
                descriptorKey));
        assertEquals(
            physical.path("descriptor").asText(),
            keys.meshNodeDescriptorRowKey(descriptorKey));
        assertEquals(
            physical.path("admission").asText(),
            keys.meshNodeDescriptorMetadataKey(descriptorKey));
        assertEquals(
            physical.path("descriptorIndex").asText(),
            keys.kindIndexKey("mesh-node"));
        assertEquals(
            physical.path("descriptorOwnerIndex").asText(),
            keys.meshNodeOwnerTokenIndexKey(
                "mesh-owner-a",
                9));
        assertEquals(
            physical.path("ownerLease").asText(),
            keys.leaseKey("mesh-owner-a"));
    }

    @Test
    void redisLocationSchemaGateRejectsIncompatibleMarker()
        throws Exception {
        String endpoint =
            System.getenv("ZLINK_REDIS_LOCATION_ENDPOINT");
        assumeTrue(
            endpoint != null && !endpoint.isBlank(),
            "ZLINK_REDIS_LOCATION_ENDPOINT is not set");
        String prefix =
            "zlink:schema-gate-test:" + UUID.randomUUID();
        var options = new ZLinkRedisLocationOptions()
            .setConnectionString(endpoint)
            .setKeyPrefix(prefix);
        RedisClient client = RedisClient.create(options.redisUri());
        try (var connection = client.connect()) {
            connection.sync().hset(
                new ZLinkRedisLocationKeys(prefix).schemaKey(),
                Map.of(
                    "format", "different-schema",
                    "epoch", "9"));
        } finally {
            client.shutdown();
        }
        try (var store = new ZLinkRedisLocationStore(options)) {
            var failure = assertThrows(
                java.util.concurrent.ExecutionException.class,
                () -> store.read(
                        "zla1:a:schema-gate",
                        () -> false)
                    .toCompletableFuture().get());
            assertInstanceOf(
                IllegalStateException.class,
                failure.getCause());
        }
    }

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
        String keyPrefix =
            "zlink:authority-test:" + UUID.randomUUID();
        try (ZLinkRedisLocationStore store = new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions()
                .setConnectionString(endpoint)
                .setKeyPrefix(keyPrefix))) {
            var owner = assertInstanceOf(
                ZLinkOwnerLeaseClaimed.class,
                store.claimOwnerLease("owner-a", Duration.ofSeconds(30))
                    .toCompletableFuture().get()).token();
            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                store.updateMeshNode(
                        descriptor(
                            NODE_A,
                            1,
                            1,
                            owner,
                            "player",
                            8,
                            4),
                        ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture().get().status());
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
            var physicalKeys = new ZLinkRedisLocationKeys(keyPrefix);
            RedisClient inspectionClient = RedisClient.create(
                new ZLinkRedisLocationOptions()
                    .setConnectionString(endpoint)
                    .setKeyPrefix(keyPrefix)
                    .redisUri());
            try (var inspection = inspectionClient.connect()) {
                var redis = inspection.sync();
                assertEquals(
                    Map.of(
                        "format", "location-authority-hybrid-v1",
                        "epoch", "1"),
                    redis.hgetall(physicalKeys.schemaKey()));
                assertEquals(
                    Set.of("ownerId", "generation", "expiresAt"),
                    Set.copyOf(redis.hkeys(
                        physicalKeys.leaseKey(owner.ownerId()))));
                assertEquals(
                    0,
                    redis.exists(
                        physicalKeys.legacyLeaseKey(
                            owner.ownerId())));
                assertEquals(
                    Set.of(
                        "owner", "gen", "json",
                        "updatedAtMs", "mesh"),
                    Set.copyOf(redis.hkeys(
                        physicalKeys.meshNodeDescriptorRowKey(
                            request.targetDescriptor()))));
                assertTrue(redis.exists(
                    physicalKeys.meshNodeDescriptorMetadataKey(
                        request.targetDescriptor())) > 0);
                assertEquals(
                    Set.copyOf(JSON.convertValue(
                        descriptorFixture().path(
                            "admissionHashFields"),
                        JSON.getTypeFactory()
                            .constructCollectionType(
                                List.class,
                                String.class))),
                    Set.copyOf(redis.hkeys(
                        physicalKeys
                            .meshNodeDescriptorMetadataKey(
                                request.targetDescriptor()))));
                assertEquals(
                    ZLinkRedisLocationKeyCodec
                        .encodeMeshNodeKey(
                            request.targetDescriptor()),
                    redis.hget(
                        physicalKeys
                            .meshNodeDescriptorMetadataKey(
                                request.targetDescriptor()),
                        "descriptorKey"));
                assertEquals(
                    owner.ownerId(),
                    redis.hget(
                        physicalKeys
                            .meshNodeDescriptorMetadataKey(
                                request.targetDescriptor()),
                        "ownerId"));
                assertEquals(
                    Long.toString(owner.leaseGeneration()),
                    redis.hget(
                        physicalKeys
                            .meshNodeDescriptorMetadataKey(
                                request.targetDescriptor()),
                        "ownerLeaseGeneration"));
                assertEquals(
                    "server",
                    redis.hget(
                        physicalKeys
                            .meshNodeDescriptorMetadataKey(
                                request.targetDescriptor()),
                        "objectRole"));
                assertEquals(
                    "1",
                    redis.hget(
                        physicalKeys
                            .meshNodeDescriptorMetadataKey(
                                request.targetDescriptor()),
                        "runtimeState"));
                assertEquals(
                    Set.of(
                        ZLinkRedisLocationKeyCodec
                            .encodeMeshNodeKey(
                                request.targetDescriptor())),
                    redis.smembers(
                        physicalKeys
                            .meshNodeOwnerTokenIndexKey(
                                owner.ownerId(),
                                owner.leaseGeneration())));
                assertTrue(redis.exists(
                    physicalKeys.authorityRowKey(
                        request.authorityKey())) > 0);
                assertEquals(
                    Set.copyOf(JSON.convertValue(
                        authorityFixture().path(
                            "currentHashFields"),
                        JSON.getTypeFactory()
                            .constructCollectionType(
                                List.class,
                                String.class))),
                    Set.copyOf(redis.hkeys(
                        physicalKeys.authorityRowKey(
                            request.authorityKey()))));
                assertEquals(
                    "actor",
                    redis.hget(
                        physicalKeys.authorityRowKey(
                            request.authorityKey()),
                        "objectKind"));
                assertTrue(redis.exists(
                    physicalKeys.creationKey(
                        reserved.reservation()
                            .reservationVersion())) > 0);
                assertTrue(redis.exists(
                    physicalKeys.capacityTypePendingKey()) > 0);
                assertTrue(redis.exists(
                    physicalKeys.capacityNodePendingKey()) > 0);
                String descriptorIdentity =
                    ZLinkRedisLocationKeyCodec.encodeMeshNodeKey(
                        request.targetDescriptor());
                String nodeBucket =
                    capacitySegment(descriptorIdentity)
                        + capacitySegment(Long.toString(
                            request
                                .targetDescriptorLifecycleGeneration()));
                String typeBucket = nodeBucket
                    + capacitySegment("actor")
                    + capacitySegment(request.stableType());
                assertEquals(
                    "1",
                    redis.hget(
                        physicalKeys.capacityNodePendingKey(),
                        nodeBucket));
                assertEquals(
                    "1",
                    redis.hget(
                        physicalKeys.capacityTypePendingKey(),
                        typeBucket));
            } finally {
                inspectionClient.shutdown();
            }
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
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkObjectCommitResult.ALREADY_COMMITTED,
                store.commit(
                        reserved.reservation(),
                        new byte[] {1, 2},
                        () -> false)
                    .toCompletableFuture().get());
            RedisClient postCommitClient = RedisClient.create(
                new ZLinkRedisLocationOptions()
                    .setConnectionString(endpoint)
                    .setKeyPrefix(keyPrefix)
                    .redisUri());
            try (var inspection = postCommitClient.connect()) {
                var redis = inspection.sync();
                assertEquals(
                    0,
                    redis.exists(physicalKeys.creationKey(
                        reserved.reservation()
                            .reservationVersion())));
                assertTrue(redis.exists(
                    physicalKeys.capacityTypeActiveKey()) > 0);
                assertTrue(redis.exists(
                    physicalKeys.capacityNodeActiveKey()) > 0);
            } finally {
                postCommitClient.shutdown();
            }
            var staleReservation =
                new systems.zlink.framework.locations
                    .ZLinkObjectReservation(
                        reserved.reservation().authorityKey(),
                        reserved.reservation().storeVersion(),
                        reserved.reservation().objectGeneration(),
                        reserved.reservation()
                            .authorityOwnerGeneration(),
                        reserved.reservation().reservationVersion(),
                        reserved.reservation().targetDescriptor(),
                        reserved.reservation()
                            .targetDescriptorLifecycleGeneration() + 1,
                        reserved.reservation().targetOwner());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkObjectCommitResult.STALE,
                store.commit(
                        staleReservation,
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
            String secondKey = "zla1:a:4:game:7:actor-2";
            var secondReservation = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkObjectReserved.class,
                store.reserve(
                        reservationRequest(
                            secondKey,
                            descriptor(
                                NODE_A,
                                1,
                                1,
                                owner,
                                "player",
                                8,
                                4),
                            owner,
                            "player"),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkObjectCommitResult.COMMITTED,
                store.commit(
                        secondReservation.reservation(),
                        new byte[] {5},
                        () -> false)
                    .toCompletableFuture().get());
            var page = assertInstanceOf(
                systems.zlink.framework.locations.ZLinkAuthorityPage.class,
                store.list("zla1:a:", Optional.empty(), 1, () -> false)
                    .toCompletableFuture().get());
            assertEquals(List.of(request.authorityKey()),
                page.items().stream()
                    .map(systems.zlink.framework.locations.ZLinkAuthorityEntry::key)
                    .toList());
            var secondCurrent = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAuthoritySnapshot.class,
                store.read(secondKey, () -> false)
                    .toCompletableFuture().get());
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAuthorityStored.class,
                store.compareExchange(
                        secondKey,
                        new systems.zlink.framework.locations
                            .ZLinkAuthorityExpectFound(
                                secondCurrent.storeVersion()),
                        new systems.zlink.framework.locations
                            .ZLinkAuthorityPut(
                                new byte[] {6},
                                systems.zlink.framework.locations
                                    .ZLinkAuthorityGenerationTransition
                                    .PRESERVE,
                                Optional.empty(),
                                Optional.empty()),
                        () -> false)
                    .toCompletableFuture().get());
            assertInstanceOf(
                systems.zlink.framework.locations.ZLinkAuthorityDeleted.class,
                store.compareExchange(
                        request.authorityKey(),
                        new systems.zlink.framework.locations.ZLinkAuthorityExpectFound(
                            updated.storeVersion()),
                        new systems.zlink.framework.locations.ZLinkAuthorityDelete(),
                        () -> false)
                    .toCompletableFuture().get());
            String postWatermarkKey =
                "zla1:a:4:game:7:actor-3";
            var postWatermark = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkObjectReserved.class,
                store.reserve(
                        reservationRequest(
                            postWatermarkKey,
                            descriptor(
                                NODE_A,
                                1,
                                1,
                                owner,
                                "player",
                                8,
                                4),
                            owner,
                            "player"),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkObjectCommitResult.COMMITTED,
                store.commit(
                        postWatermark.reservation(),
                        new byte[] {7},
                        () -> false)
                    .toCompletableFuture().get());
            var secondPage = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAuthorityPage.class,
                store.list(
                        "zla1:a:",
                        page.nextCursor(),
                        10,
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(List.of(secondKey),
                secondPage.items().stream()
                    .map(systems.zlink.framework.locations
                        .ZLinkAuthorityEntry::key)
                    .toList());
            assertArrayEquals(
                new byte[] {5},
                secondPage.items().getFirst().snapshot().payload());
            assertTrue(secondPage.nextCursor().isEmpty());
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAuthorityScanExpired.class,
                store.list(
                        "zla1:a:",
                        page.nextCursor(),
                        10,
                        () -> false)
                    .toCompletableFuture().get());
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAuthorityPage.class,
                store.list(
                        "zla1:a:",
                        Optional.empty(),
                        100,
                        () -> false)
                    .toCompletableFuture().get());
            RedisClient gcInspectionClient = RedisClient.create(
                new ZLinkRedisLocationOptions()
                    .setConnectionString(endpoint)
                    .setKeyPrefix(keyPrefix)
                    .redisUri());
            try (var inspection = gcInspectionClient.connect()) {
                var redis = inspection.sync();
                assertEquals(
                    null,
                    redis.zscore(
                        physicalKeys.authorityIndexKey(),
                        physicalKeys.encodedAuthorityKey(
                            request.authorityKey())));
                assertTrue(
                    redis.zcard(
                        physicalKeys
                            .authorityHistoryRevisionsKey(
                                request.authorityKey()))
                        <= 2);
            } finally {
                gcInspectionClient.shutdown();
            }
        }
    }

    @Test
    void redisRelocationCapacityUsesDurableSourceAllocationAfterSourceLeaseRelease()
        throws Exception {
        String endpoint = System.getenv("ZLINK_REDIS_LOCATION_ENDPOINT");
        assumeTrue(endpoint != null && !endpoint.isBlank(),
            "ZLINK_REDIS_LOCATION_ENDPOINT is not set");
        try (ZLinkRedisLocationStore store = new ZLinkRedisLocationStore(
            new ZLinkRedisLocationOptions()
                .setConnectionString(endpoint)
                .setKeyPrefix(
                    "zlink:authority-relocation-test:"
                        + UUID.randomUUID()))) {
            var source = assertInstanceOf(
                ZLinkOwnerLeaseClaimed.class,
                store.claimOwnerLease(
                        "source-owner",
                        Duration.ofSeconds(30))
                    .toCompletableFuture().get()).token();
            var target = assertInstanceOf(
                ZLinkOwnerLeaseClaimed.class,
                store.claimOwnerLease(
                        "target-owner",
                        Duration.ofSeconds(30))
                    .toCompletableFuture().get()).token();
            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                store.updateMeshNode(
                        descriptor(
                            NODE_A,
                            7,
                            1,
                            source,
                            "player",
                            8,
                            4),
                        ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture().get().status());
            RoutingId targetRid =
                RoutingId.from(new byte[] {0x02});
            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                store.updateMeshNode(
                        descriptor(
                            targetRid,
                            9,
                            1,
                            target,
                            "player",
                            8,
                            4),
                        ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture().get().status());
            String authorityKey = "zla1:a:4:game:7:actor-2";
            var sourceDescriptor =
                new systems.zlink.framework.locations
                    .ZLinkMeshNodeDescriptorKey("game", NODE_A);
            var targetDescriptor =
                new systems.zlink.framework.locations
                    .ZLinkMeshNodeDescriptorKey(
                        "game",
                        targetRid);
            var reservation = assertInstanceOf(
                systems.zlink.framework.locations.ZLinkObjectReserved.class,
                store.reserve(
                        new systems.zlink.framework.locations
                            .ZLinkObjectReservationRequest(
                                systems.zlink.framework.locations
                                    .ZLinkPlacementObjectKind.ACTOR,
                                authorityKey,
                                "player",
                                Optional.empty(),
                                Optional.empty(),
                                "creation-root",
                                new byte[32],
                                32,
                                sourceDescriptor,
                                7,
                                source,
                                new byte[] {1},
                                1),
                        () -> false)
                    .toCompletableFuture().get()).reservation();
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkObjectCommitResult.COMMITTED,
                store.commit(reservation, new byte[] {2}, () -> false)
                    .toCompletableFuture().get());
            var current = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAuthoritySnapshot.class,
                store.read(authorityKey, () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkOwnerLeaseReleaseResult.RELEASED,
                store.releaseOwnerLease(source)
                    .toCompletableFuture().get());

            var request =
                new systems.zlink.framework.locations
                    .ZLinkRelocationCapacityReservationRequest(
                        UUID.randomUUID(),
                        authorityKey,
                        current.storeVersion(),
                        current.allocation().objectKind(),
                        current.allocation().stableType(),
                        current.allocation().descriptor(),
                        current.allocation()
                            .descriptorLifecycleGeneration(),
                        source,
                        targetDescriptor,
                        9,
                        target,
                        current.allocation().capacityDelta());
            var capacity = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityReserved.class,
                store.reserveRelocationCapacity(request, () -> false)
                    .toCompletableFuture().get());
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityAlreadyReserved.class,
                store.reserveRelocationCapacity(request, () -> false)
                    .toCompletableFuture().get());

            var mismatched =
                new systems.zlink.framework.locations
                    .ZLinkRelocationCapacityReservationRequest(
                        UUID.randomUUID(),
                        authorityKey,
                        current.storeVersion(),
                        current.allocation().objectKind(),
                        "different-type",
                        current.allocation().descriptor(),
                        current.allocation()
                            .descriptorLifecycleGeneration(),
                        source,
                        targetDescriptor,
                        9,
                        target,
                        current.allocation().capacityDelta());
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityConflict.class,
                store.reserveRelocationCapacity(mismatched, () -> false)
                    .toCompletableFuture().get());

            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityAbortResult.ABORTED,
                store.abortRelocationCapacity(
                        capacity.fence(),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityAbortResult
                    .ALREADY_ABORTED,
                store.abortRelocationCapacity(
                        capacity.fence(),
                        () -> false)
                    .toCompletableFuture().get());

            var committedRequest =
                new systems.zlink.framework.locations
                    .ZLinkRelocationCapacityReservationRequest(
                        UUID.randomUUID(),
                        authorityKey,
                        current.storeVersion(),
                        current.allocation().objectKind(),
                        current.allocation().stableType(),
                        current.allocation().descriptor(),
                        current.allocation()
                            .descriptorLifecycleGeneration(),
                        source,
                        targetDescriptor,
                        9,
                        target,
                        current.allocation().capacityDelta());
            var committedCapacity = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityReserved.class,
                store.reserveRelocationCapacity(
                        committedRequest,
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                store.removeMeshNode(targetDescriptor, target)
                    .toCompletableFuture().get());
            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                store.updateMeshNode(
                        descriptor(
                            targetRid,
                            10,
                            1,
                            target,
                            "player",
                            8,
                            4),
                        ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture().get().status());
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAuthorityConflict.class,
                store.compareExchange(
                        authorityKey,
                        new systems.zlink.framework.locations
                            .ZLinkAuthorityExpectFound(
                                current.storeVersion()),
                        new systems.zlink.framework.locations
                            .ZLinkAuthorityPut(
                                new byte[] {3},
                                systems.zlink.framework.locations
                                    .ZLinkAuthorityGenerationTransition
                                    .NEW_OWNER,
                                Optional.of(target),
                                Optional.of(
                                    committedCapacity.fence())),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityAbortResult.ABORTED,
                store.abortRelocationCapacity(
                        committedCapacity.fence(),
                        () -> false)
                    .toCompletableFuture().get());
            var finalRequest =
                new systems.zlink.framework.locations
                    .ZLinkRelocationCapacityReservationRequest(
                        UUID.randomUUID(),
                        authorityKey,
                        current.storeVersion(),
                        current.allocation().objectKind(),
                        current.allocation().stableType(),
                        current.allocation().descriptor(),
                        current.allocation()
                            .descriptorLifecycleGeneration(),
                        source,
                        targetDescriptor,
                        10,
                        target,
                        current.allocation().capacityDelta());
            var finalCapacity = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityReserved.class,
                store.reserveRelocationCapacity(
                        finalRequest,
                        () -> false)
                    .toCompletableFuture().get());
            var moved = assertInstanceOf(
                systems.zlink.framework.locations.ZLinkAuthorityStored.class,
                store.compareExchange(
                        authorityKey,
                        new systems.zlink.framework.locations
                            .ZLinkAuthorityExpectFound(
                                current.storeVersion()),
                        new systems.zlink.framework.locations
                            .ZLinkAuthorityPut(
                                new byte[] {3},
                                systems.zlink.framework.locations
                                    .ZLinkAuthorityGenerationTransition
                                    .NEW_OWNER,
                                Optional.of(target),
                                Optional.of(
                                    finalCapacity.fence())),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(target.ownerId(), moved.ownerId());
            assertEquals(targetDescriptor, moved.allocation().descriptor());
            assertEquals(
                10,
                moved.allocation()
                    .descriptorLifecycleGeneration());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityAbortResult
                    .ALREADY_COMMITTED,
                store.abortRelocationCapacity(
                        finalCapacity.fence(),
                        () -> false)
                    .toCompletableFuture().get());
        }
    }

    @Test
    void redisMeshNodeDescriptorAndCreationAdmissionAreFailClosed()
        throws Exception {
        String endpoint =
            System.getenv("ZLINK_REDIS_LOCATION_ENDPOINT");
        assumeTrue(
            endpoint != null && !endpoint.isBlank(),
            "ZLINK_REDIS_LOCATION_ENDPOINT is not set");
        try (ZLinkRedisLocationStore store =
            new ZLinkRedisLocationStore(
                new ZLinkRedisLocationOptions()
                    .setConnectionString(endpoint)
                    .setKeyPrefix(
                        "zlink:descriptor-admission-test:"
                            + UUID.randomUUID()))) {
            var owner = assertInstanceOf(
                ZLinkOwnerLeaseClaimed.class,
                store.claimOwnerLease(
                        "descriptor-owner",
                        Duration.ofSeconds(30))
                    .toCompletableFuture().get()).token();
            var initial = descriptor(
                NODE_A,
                11,
                1,
                owner,
                "player",
                2,
                1);
            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                store.updateMeshNode(
                        initial,
                        ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture().get().status());
            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                store.updateMeshNode(
                        initial,
                        ZLinkLocationWriteIntent.RENEW)
                    .toCompletableFuture().get().status());
            assertEquals(
                java.util.Set.of(
                    "owner",
                    "gen",
                    "json",
                    "updatedAtMs",
                    "mesh"),
                store.readMeshNodeHashFields(
                        new systems.zlink.framework.locations
                            .ZLinkMeshNodeDescriptorKey(
                                "game",
                                NODE_A))
                    .toCompletableFuture().get().keySet());
            var sameRevisionDifferentBytes = descriptor(
                NODE_A,
                11,
                1,
                owner,
                "different-type",
                2,
                1);
            var protocolError = assertThrows(
                java.util.concurrent.ExecutionException.class,
                () -> store.updateMeshNode(
                        sameRevisionDifferentBytes,
                        ZLinkLocationWriteIntent.RENEW)
                    .toCompletableFuture().get());
            assertInstanceOf(
                IllegalStateException.class,
                protocolError.getCause());
            var page = store.listMeshNodes(
                    "game",
                    ZLinkPageRequest.firstPage())
                .toCompletableFuture().get();
            var storedDescriptor = page.items().getFirst();
            assertEquals(initial.meshName(), storedDescriptor.meshName());
            assertEquals(
                initial.objectCapabilities(),
                storedDescriptor.objectCapabilities());
            assertEquals(
                initial.capacity(),
                storedDescriptor.capacity());

            var mutableUpdate = descriptor(
                NODE_A,
                11,
                2,
                owner,
                "player",
                2,
                1);
            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                store.updateMeshNode(
                        mutableUpdate,
                        ZLinkLocationWriteIntent.RENEW)
                    .toCompletableFuture().get().status());
            var invalidImmutableUpdate = descriptor(
                NODE_A,
                11,
                3,
                owner,
                "different-type",
                3,
                1);
            assertEquals(
                ZLinkLocationWriteStatus.IGNORED_STALE,
                store.updateMeshNode(
                        invalidImmutableUpdate,
                        ZLinkLocationWriteIntent.RENEW)
                    .toCompletableFuture().get().status());
            String firstKey = "zla1:a:descriptor-capacity-1";
            var first = assertInstanceOf(
                systems.zlink.framework.locations.ZLinkObjectReserved.class,
                store.reserve(
                        reservationRequest(
                            firstKey,
                            mutableUpdate,
                            owner,
                            "player"),
                        () -> false)
                    .toCompletableFuture().get());
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkPlacementCapacityExhausted.class,
                store.reserve(
                        reservationRequest(
                            "zla1:a:descriptor-capacity-2",
                            mutableUpdate,
                            owner,
                            "player"),
                        () -> false)
                    .toCompletableFuture().get());
            assertInstanceOf(
                systems.zlink.framework.locations.ZLinkObjectConflict.class,
                store.reserve(
                        reservationRequest(
                            "zla1:a:descriptor-unsupported-type",
                            mutableUpdate,
                            owner,
                            "unsupported"),
                        () -> false)
                    .toCompletableFuture().get());

            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                store.removeMeshNode(
                        new systems.zlink.framework.locations
                            .ZLinkMeshNodeDescriptorKey(
                                "game",
                                NODE_A),
                        owner)
                    .toCompletableFuture().get());
            var replacement = descriptor(
                NODE_A,
                12,
                1,
                owner,
                "player",
                3,
                1);
            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                store.updateMeshNode(
                        replacement,
                        ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture().get().status());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkObjectCommitResult.STALE,
                store.commit(
                        first.reservation(),
                        new byte[] {2},
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkObjectAbortResult.ABORTED,
                store.abort(first.reservation(), () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                1,
                store.removeAllByOwner(owner.ownerId())
                    .toCompletableFuture().get());
            assertEquals(
                List.of(),
                store.listMeshNodes(
                        "game",
                        ZLinkPageRequest.firstPage())
                    .toCompletableFuture().get().items());
        }
    }

    @Test
    void redisMeshNodeCapacityProjectionTracksAuthorityTransactions()
        throws Exception {
        String endpoint =
            System.getenv("ZLINK_REDIS_LOCATION_ENDPOINT");
        assumeTrue(
            endpoint != null && !endpoint.isBlank(),
            "ZLINK_REDIS_LOCATION_ENDPOINT is not set");
        try (ZLinkRedisLocationStore store =
            new ZLinkRedisLocationStore(
                new ZLinkRedisLocationOptions()
                    .setConnectionString(endpoint)
                    .setKeyPrefix(
                        "zlink:capacity-projection-test:"
                            + UUID.randomUUID()))) {
            var owner = assertInstanceOf(
                ZLinkOwnerLeaseClaimed.class,
                store.claimOwnerLease(
                        "capacity-owner",
                        Duration.ofSeconds(30))
                    .toCompletableFuture().get()).token();
            var meshNode = descriptor(
                NODE_A,
                31,
                1,
                owner,
                "player",
                8,
                8);
            assertEquals(
                ZLinkLocationWriteStatus.STORED,
                store.updateMeshNode(
                        meshNode,
                        ZLinkLocationWriteIntent.NEW_CLAIM)
                    .toCompletableFuture().get().status());

            String authorityKey =
                "zla1:a:capacity-projection";
            var reserved = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkObjectReserved.class,
                store.reserve(
                        reservationRequest(
                            authorityKey,
                            meshNode,
                            owner,
                            "player"),
                        () -> false)
                    .toCompletableFuture().get()).reservation();
            assertEquals(
                new systems.zlink.framework.locations
                    .ZLinkPlacementCapacity(0, 1, 8, 8),
                store.listMeshNodes(
                        "game",
                        ZLinkPageRequest.firstPage())
                    .toCompletableFuture().get()
                    .items().getFirst().capacity());

            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkObjectCommitResult.COMMITTED,
                store.commit(
                        reserved,
                        new byte[] {2},
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                new systems.zlink.framework.locations
                    .ZLinkPlacementCapacity(1, 0, 8, 8),
                store.listMeshNodes(
                        "game",
                        ZLinkPageRequest.firstPage())
                    .toCompletableFuture().get()
                    .items().getFirst().capacity());

            var active = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAuthoritySnapshot.class,
                store.read(authorityKey, () -> false)
                    .toCompletableFuture().get());
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAuthorityDeleted.class,
                store.compareExchange(
                        authorityKey,
                        new systems.zlink.framework.locations
                            .ZLinkAuthorityExpectFound(
                                active.storeVersion()),
                        new systems.zlink.framework.locations
                            .ZLinkAuthorityDelete(),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                new systems.zlink.framework.locations
                    .ZLinkPlacementCapacity(0, 0, 8, 8),
                store.listMeshNodes(
                        "game",
                        ZLinkPageRequest.firstPage())
                    .toCompletableFuture().get()
                    .items().getFirst().capacity());
        }
    }

    @Test
    void redisAggregatePersistsExactPrepareAndFinalizesCapacityAtomically()
        throws Exception {
        String endpoint =
            System.getenv("ZLINK_REDIS_LOCATION_ENDPOINT");
        assumeTrue(
            endpoint != null && !endpoint.isBlank(),
            "ZLINK_REDIS_LOCATION_ENDPOINT is not set");
        try (ZLinkRedisLocationStore store =
            new ZLinkRedisLocationStore(
                new ZLinkRedisLocationOptions()
                    .setConnectionString(endpoint)
                    .setKeyPrefix(
                        "zlink:aggregate-test:"
                            + UUID.randomUUID()))) {
            var source = assertInstanceOf(
                ZLinkOwnerLeaseClaimed.class,
                store.claimOwnerLease(
                        "aggregate-source",
                        Duration.ofSeconds(30))
                    .toCompletableFuture().get()).token();
            var target = assertInstanceOf(
                ZLinkOwnerLeaseClaimed.class,
                store.claimOwnerLease(
                        "aggregate-target",
                        Duration.ofSeconds(30))
                    .toCompletableFuture().get()).token();
            RoutingId sourceRid =
                RoutingId.from(new byte[] {0x11});
            RoutingId targetRid =
                RoutingId.from(new byte[] {0x12});
            var sourceDescriptor = descriptor(
                sourceRid,
                21,
                1,
                source,
                "player",
                8,
                4);
            var targetDescriptor = descriptor(
                targetRid,
                22,
                1,
                target,
                "player",
                8,
                4);
            store.updateMeshNode(
                    sourceDescriptor,
                    ZLinkLocationWriteIntent.NEW_CLAIM)
                .toCompletableFuture().get();
            store.updateMeshNode(
                    targetDescriptor,
                    ZLinkLocationWriteIntent.NEW_CLAIM)
                .toCompletableFuture().get();

            List<String> authorityKeys = List.of(
                "zla1:a:aggregate-a",
                "zla1:a:aggregate-b");
            List<systems.zlink.framework.locations
                .ZLinkAuthoritySnapshot> snapshots =
                new java.util.ArrayList<>();
            for (String authorityKey : authorityKeys) {
                var reservation = assertInstanceOf(
                    systems.zlink.framework.locations
                        .ZLinkObjectReserved.class,
                    store.reserve(
                            reservationRequest(
                                authorityKey,
                                sourceDescriptor,
                                source,
                                "player"),
                            () -> false)
                        .toCompletableFuture().get());
                assertEquals(
                    systems.zlink.framework.locations
                        .ZLinkObjectCommitResult.COMMITTED,
                    store.commit(
                            reservation.reservation(),
                            new byte[] {1},
                            () -> false)
                        .toCompletableFuture().get());
                snapshots.add(assertInstanceOf(
                    systems.zlink.framework.locations
                        .ZLinkAuthoritySnapshot.class,
                    store.read(authorityKey, () -> false)
                        .toCompletableFuture().get()));
            }

            List<systems.zlink.framework.locations
                .ZLinkRelocationCapacityFence> capacityFences =
                new java.util.ArrayList<>();
            List<systems.zlink.framework.locations
                .ZLinkRelocationCapacityReservationRequest>
                capacityRequests = new java.util.ArrayList<>();
            for (int index = 0;
                index < authorityKeys.size();
                index++) {
                var snapshot = snapshots.get(index);
                var capacityRequest =
                    new systems.zlink.framework.locations
                        .ZLinkRelocationCapacityReservationRequest(
                            UUID.randomUUID(),
                            authorityKeys.get(index),
                            snapshot.storeVersion(),
                            snapshot.allocation().objectKind(),
                            snapshot.allocation().stableType(),
                            snapshot.allocation().descriptor(),
                            snapshot.allocation()
                                .descriptorLifecycleGeneration(),
                            source,
                            new systems.zlink.framework.locations
                                .ZLinkMeshNodeDescriptorKey(
                                    "game",
                                    targetRid),
                            22,
                            target,
                            1);
                capacityRequests.add(capacityRequest);
                var reserved = assertInstanceOf(
                    systems.zlink.framework.locations
                        .ZLinkRelocationCapacityReserved.class,
                    store.reserveRelocationCapacity(
                            capacityRequest,
                            () -> false)
                        .toCompletableFuture().get());
                capacityFences.add(reserved.fence());
            }
            UUID aggregateId = UUID.randomUUID();
            var participants =
                java.util.stream.IntStream.range(
                        0,
                        authorityKeys.size())
                    .mapToObj(index ->
                        new systems.zlink.framework.locations
                            .ZLinkAggregateParticipant(
                                authorityKeys.get(index),
                                snapshots.get(index).storeVersion(),
                                systems.zlink.framework.locations
                                    .ZLinkAuthorityGenerationTransition
                                    .NEW_OWNER,
                                new byte[] {(byte) (3 + index)},
                                new byte[] {(byte) (5 + index)}))
                    .toList();
            var prepareRequest =
                new systems.zlink.framework.locations
                    .ZLinkAggregatePrepareRequest(
                        aggregateId,
                        1,
                        participants,
                        new byte[32],
                        List.of(
                            capacityFences.get(1),
                            capacityFences.get(0)),
                        target);
            var prepared = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAggregatePrepared.class,
                store.prepareAggregate(
                        prepareRequest,
                        () -> false)
                    .toCompletableFuture().get());
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAggregateAlreadyPrepared.class,
                store.prepareAggregate(
                        prepareRequest,
                        () -> false)
                    .toCompletableFuture().get());
            var changed = new java.util.ArrayList<>(participants);
            changed.set(
                0,
                new systems.zlink.framework.locations
                    .ZLinkAggregateParticipant(
                        participants.getFirst().authorityKey(),
                        participants.getFirst().expectedStoreVersion(),
                        participants.getFirst().ownerTransition(),
                        new byte[] {99},
                        participants.getFirst()
                            .membershipMutation()));
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAggregateConflict.class,
                store.prepareAggregate(
                        new systems.zlink.framework.locations
                            .ZLinkAggregatePrepareRequest(
                                aggregateId,
                                1,
                                changed,
                                new byte[32],
                                capacityFences,
                                target),
                        () -> false)
                    .toCompletableFuture().get());
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAggregateStale.class,
                store.prepareAggregate(
                        new systems.zlink.framework.locations
                            .ZLinkAggregatePrepareRequest(
                                aggregateId,
                                2,
                                participants,
                                new byte[32],
                                capacityFences,
                                target),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityAbortResult.STALE,
                store.abortRelocationCapacity(
                        capacityFences.getFirst(),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkAggregateCommitResult.COMMITTED,
                store.commitAggregate(
                        prepared.fence(),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkAggregateCommitResult.ALREADY_COMMITTED,
                store.commitAggregate(
                        prepared.fence(),
                        () -> false)
                    .toCompletableFuture().get());
            assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityAlreadyReserved.class,
                store.reserveRelocationCapacity(
                        capacityRequests.getFirst(),
                        () -> false)
                    .toCompletableFuture().get());
            List<systems.zlink.framework.locations
                .ZLinkAuthoritySnapshot> movedSnapshots =
                new java.util.ArrayList<>();
            for (String authorityKey : authorityKeys) {
                var moved = assertInstanceOf(
                    systems.zlink.framework.locations
                        .ZLinkAuthoritySnapshot.class,
                    store.read(authorityKey, () -> false)
                        .toCompletableFuture().get());
                assertEquals(target.ownerId(), moved.ownerId());
                movedSnapshots.add(moved);
            }
            assertArrayEquals(
                new byte[] {5},
                store.readAuthorityMembershipMutation(
                        authorityKeys.getFirst())
                    .toCompletableFuture().get());
            assertArrayEquals(
                new byte[] {6},
                store.readAuthorityMembershipMutation(
                        authorityKeys.get(1))
                    .toCompletableFuture().get());

            List<systems.zlink.framework.locations
                .ZLinkRelocationCapacityFence> reverseFences =
                new java.util.ArrayList<>();
            for (int index = 0;
                index < authorityKeys.size();
                index++) {
                var moved = movedSnapshots.get(index);
                reverseFences.add(assertInstanceOf(
                    systems.zlink.framework.locations
                        .ZLinkRelocationCapacityReserved.class,
                    store.reserveRelocationCapacity(
                            new systems.zlink.framework.locations
                                .ZLinkRelocationCapacityReservationRequest(
                                    UUID.randomUUID(),
                                    authorityKeys.get(index),
                                    moved.storeVersion(),
                                    moved.allocation().objectKind(),
                                    moved.allocation().stableType(),
                                    moved.allocation().descriptor(),
                                    moved.allocation()
                                        .descriptorLifecycleGeneration(),
                                    target,
                                    new systems.zlink.framework.locations
                                        .ZLinkMeshNodeDescriptorKey(
                                            "game",
                                            sourceRid),
                                    21,
                                    source,
                                    1),
                            () -> false)
                        .toCompletableFuture().get()).fence());
            }
            var reverseParticipants =
                java.util.stream.IntStream.range(
                        0,
                        authorityKeys.size())
                    .mapToObj(index ->
                        new systems.zlink.framework.locations
                            .ZLinkAggregateParticipant(
                                authorityKeys.get(index),
                                movedSnapshots.get(index)
                                    .storeVersion(),
                                systems.zlink.framework.locations
                                    .ZLinkAuthorityGenerationTransition
                                    .NEW_OWNER,
                                new byte[] {(byte) (7 + index)},
                                new byte[] {(byte) (9 + index)}))
                    .toList();
            var reversePrepared = assertInstanceOf(
                systems.zlink.framework.locations
                    .ZLinkAggregatePrepared.class,
                store.prepareAggregate(
                        new systems.zlink.framework.locations
                            .ZLinkAggregatePrepareRequest(
                                UUID.randomUUID(),
                                1,
                                reverseParticipants,
                                new byte[32],
                                reverseFences,
                                source),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkAggregateAbortResult.ABORTED,
                store.abortAggregate(
                        reversePrepared.fence(),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkAggregateAbortResult.ALREADY_ABORTED,
                store.abortAggregate(
                        reversePrepared.fence(),
                        () -> false)
                    .toCompletableFuture().get());
            assertEquals(
                systems.zlink.framework.locations
                    .ZLinkRelocationCapacityAbortResult
                    .ALREADY_ABORTED,
                store.abortRelocationCapacity(
                        reverseFences.getFirst(),
                        () -> false)
                    .toCompletableFuture().get());
            for (String authorityKey : authorityKeys) {
                assertEquals(
                    target.ownerId(),
                    assertInstanceOf(
                        systems.zlink.framework.locations
                            .ZLinkAuthoritySnapshot.class,
                        store.read(authorityKey, () -> false)
                            .toCompletableFuture().get())
                        .ownerId());
            }
        }
    }

    private static JsonNode authorityFixture() throws Exception {
        return redisFixture("authority-store-v1.json");
    }

    private static JsonNode descriptorFixture() throws Exception {
        return redisFixture("mesh-node-descriptor-v1.json");
    }

    private static JsonNode redisFixture(String fileName)
        throws Exception {
        Path current = Path.of("").toAbsolutePath();
        while (current != null) {
            for (Path candidate : List.of(
                current.resolve(
                    "framework/testdata/location/redis/"
                        + fileName),
                current.resolve(
                    "testdata/location/redis/"
                        + fileName))) {
                if (Files.isRegularFile(candidate)) {
                    return JSON.readTree(candidate.toFile());
                }
            }
            current = current.getParent();
        }
        throw new IllegalStateException(
            fileName + " was not found");
    }

    private static String capacityTypeBucket(
        String descriptor,
        String lifecycle,
        String kind,
        String stableType) {
        return capacitySegment(descriptor)
            + capacitySegment(lifecycle)
            + capacitySegment(kind)
            + capacitySegment(stableType);
    }

    private static String capacitySegment(String value) {
        return value.getBytes(StandardCharsets.UTF_8).length
            + ":"
            + value;
    }

    private static systems.zlink.framework.locations
        .ZLinkObjectReservationRequest reservationRequest(
            String authorityKey,
            systems.zlink.framework.locations.ZLinkMeshNodeDescriptor
                descriptor,
            ZLinkLocationOwnerToken owner,
            String stableType) {
        return new systems.zlink.framework.locations
            .ZLinkObjectReservationRequest(
                systems.zlink.framework.locations
                    .ZLinkPlacementObjectKind.ACTOR,
                authorityKey,
                stableType,
                Optional.empty(),
                Optional.empty(),
                "creation-root",
                new byte[32],
                32,
                new systems.zlink.framework.locations
                    .ZLinkMeshNodeDescriptorKey(
                        descriptor.meshName(),
                        descriptor.rid()),
                descriptor.lifecycleGeneration(),
                owner,
                new byte[] {1},
                1);
    }

    private static systems.zlink.framework.locations
        .ZLinkMeshNodeDescriptor descriptor(
            RoutingId rid,
            long lifecycleGeneration,
            long descriptorRevision,
            ZLinkLocationOwnerToken owner,
            String stableType,
            int activeLimit,
            int pendingLimit) {
        return new systems.zlink.framework.locations
            .ZLinkMeshNodeDescriptor(
                "game",
                rid,
                lifecycleGeneration,
                descriptorRevision,
                "tcp://127.0.0.1:7000",
                Map.of("game", 100),
                1,
                List.of(
                    new systems.zlink.framework.locations
                        .ZLinkObjectCapability(
                            systems.zlink.framework.locations
                                .ZLinkPlacementObjectKind.ACTOR,
                            stableType,
                            systems.zlink.framework.locations
                                .ZLinkObjectMaintenancePolicyKind
                                .SNAPSHOT,
                            true,
                            java.util.Set.of("default"),
                            activeLimit,
                            pendingLimit)),
                systems.zlink.framework.locations
                    .ZLinkMeshNodeObjectRole.SERVER,
                100,
                new systems.zlink.framework.locations
                    .ZLinkPlacementCapacity(
                        0,
                        0,
                        activeLimit,
                        pendingLimit),
                Optional.empty(),
                systems.zlink.framework.runtime.host
                    .ZLinkFrameworkRuntimeState.SERVING,
                "security",
                owner.ownerId(),
                owner.leaseGeneration(),
                UPDATED_AT);
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
