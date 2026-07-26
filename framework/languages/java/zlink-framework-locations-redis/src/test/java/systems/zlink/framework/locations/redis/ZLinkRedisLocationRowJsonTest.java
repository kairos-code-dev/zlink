package systems.zlink.framework.locations.redis;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkActivationConcurrency;
import systems.zlink.framework.locations.ZLinkFanoutPublisherDescriptor;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkMeshNodeDescriptor;
import systems.zlink.framework.locations.ZLinkMeshNodeObjectRole;
import systems.zlink.framework.locations.ZLinkPlacementCapacity;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.spots.ZLinkSpotKind;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState;

class ZLinkRedisLocationRowJsonTest {
    private static final ObjectMapper JSON = new ObjectMapper();
    private static final Instant UPDATED_AT = Instant.parse("2026-07-03T00:00:00Z");

    @Test
    void fanoutPublisherRowRoundTripsDedicatedDescriptorFields() {
        ZLinkFanoutPublisherDescriptor original =
            new ZLinkFanoutPublisherDescriptor(
                "events",
                RoutingId.from(new byte[] {0x00, (byte) 0xff, 0x01}),
                7,
                11,
                "tcp://127.0.0.1:7400",
                ZLinkFrameworkRuntimeState.SERVING,
                "cluster-a",
                "owner-a",
                3,
                UPDATED_AT);

        ZLinkFanoutPublisherDescriptor decoded =
            ZLinkRedisLocationRowJson.deserializeFanoutPublisher(
                ZLinkRedisLocationRowJson.serializeFanoutPublisher(original),
                3,
                UPDATED_AT);

        assertEquals(original, decoded);
        assertEquals(
            ZLinkRedisLocationRowJson
                .fanoutPublisherImmutableFingerprint(original),
            ZLinkRedisLocationRowJson
                .fanoutPublisherImmutableFingerprint(decoded));
    }

    @Test
    void canonicalHybridSchemaUsesOneFixedProviderHashTag() {
        assertThrows(
            IllegalArgumentException.class,
            () -> new ZLinkRedisLocationOptions()
                .setKeyPrefix("bad:{caller-tag}"));
        var keys = new ZLinkRedisLocationKeys("app");
        assertEquals(
            "app:{zlink-location-v3}:schema",
            keys.schemaKey());
        assertEquals(
            "app:{zlink-location-v3}:counter",
            keys.counterKey());
        assertEquals(
            "app:{zlink-location-v3}:authority:key-index",
            keys.authorityIndexKey());
        assertEquals(
            "app:{zlink-location-v3}:membership:current",
            keys.authorityMembershipsKey());
        String current = keys.authorityRowKey(
            "zla1:a:4:game:7:actor-1");
        assertTrue(current.matches(
            "app:\\{zlink-location-v3}:authority:current:"
                + "[0-9a-f]{64}"));
        assertTrue(keys.leaseKey("owner-a").matches(
            "app:\\{zlink-location-v3}:owner-lease:"
                + "[0-9a-f]{64}"));
    }

    @Test
    void peerJsonUsesDotnetNumericEnumValuesAndHexRoutingId() throws Exception {
        String json = ZLinkRedisLocationRowJson.serializePeer(new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "mesh",
            RoutingId.from(new byte[] {0x01, 0x23}),
            ZLinkLocationRole.ROUTER,
            "tcp://127.0.0.1:6000",
            10,
            true,
            20,
            Map.of("pub-endpoint", "tcp://127.0.0.1:6001"),
            List.of("a", "b"),
            "owner-a",
            7,
            UPDATED_AT));

        JsonNode node = JSON.readTree(json);
        assertEquals(1, node.path("AutoConnectType").asInt());
        assertEquals(3, node.path("Role").asInt());
        assertEquals("0123", node.path("NodeRid").asText());
        assertEquals(true, node.path("Draining").asBoolean());
        assertEquals("tcp://127.0.0.1:6001", node.path("Metadata").path("pub-endpoint").asText());

        ZLinkPeerLocation decoded = ZLinkRedisLocationRowJson.deserializePeer(json, 9, UPDATED_AT.plusSeconds(1));
        assertEquals(true, decoded.draining());
        assertEquals(9, decoded.generation());
        assertEquals("owner-a", decoded.ownerId());
    }

    @Test
    void routeJsonStoresValueAsBase64AndRestoresStoreFields() throws Exception {
        byte[] value = new byte[] {0x01, 0x02, 0x03};
        String json = ZLinkRedisLocationRowJson.serializeRoute(new ZLinkRouteLocation(
            ZLinkRouteKind.ACTOR_SESSION,
            "route-key",
            RoutingId.from(new byte[] {0x0a}),
            "owner-a",
            3,
            value,
            UPDATED_AT));

        assertEquals(Base64.getEncoder().encodeToString(value), JSON.readTree(json).path("Value").asText());
        ZLinkRouteLocation row = ZLinkRedisLocationRowJson.deserializeRoute(json, 9, UPDATED_AT.plusSeconds(1));

        assertEquals(ZLinkRouteKind.ACTOR_SESSION, row.routeKind());
        assertEquals(9, row.generation());
        assertEquals(UPDATED_AT.plusSeconds(1), row.updatedAt());
        assertArrayEquals(value, row.value());
    }

    @Test
    void spotJsonPreservesLifecycleGenerationSeparatelyFromOwnerGeneration() throws Exception {
        ZLinkSpotLocation original = new ZLinkSpotLocation(
            "mesh",
            "spot",
            41L,
            "room",
            RoutingId.from("node"),
            ZLinkSpotKind.USER,
            null,
            "owner-a",
            7L,
            UPDATED_AT);

        String json = ZLinkRedisLocationRowJson.serializeSpot(original);
        assertEquals(41L, JSON.readTree(json).path("SpotGeneration").asLong());

        ZLinkSpotLocation decoded =
            ZLinkRedisLocationRowJson.deserializeSpot(json, 9L, UPDATED_AT.plusSeconds(1));
        assertEquals(41L, decoded.spotGeneration());
        assertEquals(9L, decoded.generation());
    }

    @Test
    void actorDeserializerAcceptsTypedRefAndRequiredSpotMeshName() {
        String json = """
            {
              "ActorType": "game",
              "ActorId": "actor-1",
              "ActorRef": {
                "actorId": "actor-1",
                "objectGeneration": "7",
                "meshName": "game",
                "nodeRid": "01"
              },
              "NodeRid": "01",
              "Generation": 1,
              "LocationKind": 2,
              "SpotMeshName": "game",
              "SpotId": "02",
              "OwnerId": "owner-a",
              "UpdatedAt": "2026-07-03T00:00:00Z"
            }
            """;

        ZLinkActorLocation row = ZLinkRedisLocationRowJson.deserializeActor(json, 5, UPDATED_AT.plusSeconds(2));

        assertEquals("game", row.actorType());
        assertEquals(
            new ActorRef("actor-1", 7, "game", RoutingId.fromHex("01")),
            row.actorRef());
        assertEquals(ZLinkSpotKind.USER, row.locationKind());
        assertEquals("game", row.spotMeshName());
        assertEquals(5, row.generation());
        assertEquals(UPDATED_AT.plusSeconds(2), row.updatedAt());
    }

    @Test
    void actorLocationV2FixturePinsCanonicalRedisShape() throws Exception {
        JsonNode root = JSON.readTree(Files.readString(fixturePath()));

        assertEquals(
            List.of(
                "payload",
                "storeVersion",
                "objectGeneration",
                "authorityOwnerGeneration",
                "ownerId",
                "ownerLeaseGeneration"),
            JSON.convertValue(
                root.path("hashFields"),
                JSON.getTypeFactory().constructCollectionType(List.class, String.class)));
        JsonNode row = root.path("row");
        assertEquals("actor", row.path("kind").asText());
        assertEquals("zla1:a:7:actor-1", row.path("key").asText());
        JsonNode hash = row.path("hash");
        assertEquals("opaque-actor-authority-v1", hash.path("payload").asText());
        assertEquals("101", hash.path("storeVersion").asText());
        assertEquals("11", hash.path("objectGeneration").asText());
        assertEquals("4", hash.path("authorityOwnerGeneration").asText());
        assertEquals("actor-owner-a", hash.path("ownerId").asText());
        assertEquals("9", hash.path("ownerLeaseGeneration").asText());
    }

    @Test
    void meshNodeDescriptorFixturePinsCanonicalJsonBytes()
        throws Exception {
        JsonNode root = JSON.readTree(Files.readString(
            descriptorFixturePath()));
        JsonNode hash = root.path("row").path("hash");
        ZLinkMeshNodeDescriptor descriptor =
            new ZLinkMeshNodeDescriptor(
                "game",
                RoutingId.fromHex("67616d652d61"),
                7,
                3,
                "tcp://10.0.0.1:7300",
                Map.of("orders", 100, "world", 50),
                0,
                List.of(),
                ZLinkMeshNodeObjectRole.NONE,
                Optional.empty(),
                100,
                new ZLinkPlacementCapacity(
                    new systems.zlink.framework.locations
                        .ZLinkCapacityUsage(0, 0, 10_000),
                    new systems.zlink.framework.locations
                        .ZLinkCapacityUsage(0, 0, 128),
                    List.of()),
                new ZLinkActivationConcurrency(0, 128),
                Optional.empty(),
                ZLinkFrameworkRuntimeState.SERVING,
                "cluster-a",
                "mesh-owner-a",
                9,
                Instant.parse("2024-07-15T00:00:00Z"));

        assertEquals(
            hash.path("json").asText(),
            ZLinkRedisLocationRowJson.serializeMeshNode(descriptor));
        JsonNode immutableDigest = root.path("immutableDigest");
        assertEquals(
            immutableDigest.path("preimage").asText(),
            ZLinkRedisLocationRowJson.meshNodeImmutablePreimage(
                descriptor));
        assertEquals(
            immutableDigest.path("sha256LowerHex").asText(),
            ZLinkRedisLocationRowJson.meshNodeImmutableFingerprint(
                descriptor));
        assertEquals(
            Long.toString(descriptor.lifecycleGeneration()),
            hash.path("gen").asText());
    }

    private static Path fixturePath() {
        Path current = Path.of("").toAbsolutePath();
        while (current != null) {
            Path candidate = current.resolve("framework/testdata/location/redis/actor-location-v2.json");
            if (Files.exists(candidate)) {
                return candidate;
            }
            candidate = current.resolve("testdata/location/redis/actor-location-v2.json");
            if (Files.exists(candidate)) {
                return candidate;
            }
            current = current.getParent();
        }
        throw new IllegalStateException("Could not find framework/testdata/location/redis/actor-location-v2.json");
    }

    private static Path descriptorFixturePath() {
        Path current = Path.of("").toAbsolutePath();
        while (current != null) {
            Path candidate = current.resolve(
                "framework/testdata/location/redis/"
                    + "mesh-node-descriptor-v1.json");
            if (Files.exists(candidate)) {
                return candidate;
            }
            candidate = current.resolve(
                "testdata/location/redis/mesh-node-descriptor-v1.json");
            if (Files.exists(candidate)) {
                return candidate;
            }
            current = current.getParent();
        }
        throw new IllegalStateException(
            "Could not find mesh-node-descriptor-v1.json");
    }

    private static ZLinkActorLocation fixtureActor() {
        return new ZLinkActorLocation(
            "actor-1",
            "player",
            new ActorRef(
                "actor-1",
                1,
                "play",
                RoutingId.from("node-1")),
            RoutingId.from("node-1"),
            ZLinkSpotKind.ENTRY,
            "play",
            null,
            "owner-a",
            0,
            Instant.parse("0001-01-01T00:00:00Z"));
    }

    private static ZLinkPeerLocation fixturePeer() {
        return new ZLinkPeerLocation(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "play",
            RoutingId.from("node-1"),
            ZLinkLocationRole.ROUTER,
            "tcp://127.0.0.1:5001",
            100,
            false,
            7,
            Map.of("route-endpoint", "tcp://127.0.0.1:6001"),
            List.of("router", "route-bridge"),
            "owner-a",
            0,
            Instant.parse("0001-01-01T00:00:00Z"));
    }

    private static ZLinkSpotLocation fixtureSpot() {
        return new ZLinkSpotLocation(
            "play",
            "spot-1",
            "game",
            RoutingId.from("node-1"),
            ZLinkSpotKind.USER,
            null,
            "owner-a",
            0,
            Instant.parse("0001-01-01T00:00:00Z"));
    }

    private static ZLinkRouteLocation fixtureRoute() {
        return new ZLinkRouteLocation(
            ZLinkRouteKind.ACTOR_SESSION,
            "route-1",
            RoutingId.from("node-1"),
            "owner-a",
            0,
            new byte[] {0x01, 0x02, 0x03, 0x04},
            Instant.parse("0001-01-01T00:00:00Z"));
    }
}
