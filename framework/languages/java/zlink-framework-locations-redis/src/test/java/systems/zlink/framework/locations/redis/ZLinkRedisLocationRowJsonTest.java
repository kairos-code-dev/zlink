package systems.zlink.framework.locations.redis;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkRouteLocation;
import systems.zlink.framework.locations.ZLinkSpotLocation;
import systems.zlink.framework.spots.ZLinkSpotKind;

class ZLinkRedisLocationRowJsonTest {
    private static final ObjectMapper JSON = new ObjectMapper();
    private static final Instant UPDATED_AT = Instant.parse("2026-07-03T00:00:00Z");

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
            RoutingId.from("spot"),
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
                "nodeRid": "01",
                "actorId": "actor-1",
                "generation": 7
              },
              "NodeRid": "01",
              "Generation": 1,
              "LocationKind": 2,
              "SpotMeshName": "game",
              "SpotRid": "02",
              "OwnerId": "owner-a",
              "UpdatedAt": "2026-07-03T00:00:00Z"
            }
            """;

        ZLinkActorLocation row = ZLinkRedisLocationRowJson.deserializeActor(json, 5, UPDATED_AT.plusSeconds(2));

        assertEquals("game", row.actorType());
        assertEquals(new ActorRef(RoutingId.fromHex("01"), "actor-1", 7), row.actorRef());
        assertEquals(ZLinkSpotKind.USER, row.locationKind());
        assertEquals("game", row.spotMeshName());
        assertEquals(5, row.generation());
        assertEquals(UPDATED_AT.plusSeconds(2), row.updatedAt());
    }

    @Test
    void actorLocationV2FixturePinsCanonicalRedisShape() throws Exception {
        JsonNode root = JSON.readTree(Files.readString(fixturePath()));

        assertEquals(
            List.of("owner", "gen", "json", "updatedAtMs", "mesh"),
            JSON.convertValue(
                root.path("hashFields"),
                JSON.getTypeFactory().constructCollectionType(List.class, String.class)));
        JsonNode row = root.path("row");
        assertEquals("actor", row.path("kind").asText());
        assertEquals("4:game7:actor-1", row.path("key").asText());
        JsonNode hash = row.path("hash");
        assertEquals("actor-owner-a", hash.path("owner").asText());
        assertEquals("5", hash.path("gen").asText());
        assertEquals("1721001600000", hash.path("updatedAtMs").asText());
        assertEquals("game", hash.path("mesh").asText());
        JsonNode payload = JSON.readTree(hash.path("json").asText());
        assertEquals("game", payload.path("MeshName").asText());
        assertEquals(3L, payload.path("SpotGeneration").asLong());
        assertEquals(7L, payload.path("OwnerNodeGeneration").asLong());
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

    private static ZLinkActorLocation fixtureActor() {
        return new ZLinkActorLocation(
            "actor-1",
            "player",
            new ActorRef(RoutingId.from("node-1"), "actor-1", 1),
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
            RoutingId.from("spot-1"),
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
