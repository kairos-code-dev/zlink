package systems.zlink.framework.locations.redis;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.time.Instant;
import java.util.Base64;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkPeerLocation;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkRouteLocation;
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
        assertEquals("tcp://127.0.0.1:6001", node.path("Metadata").path("pub-endpoint").asText());
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
    void actorDeserializerAcceptsDotnetJsonWithoutSpotMeshName() {
        String json = """
            {
              "ActorType": "game",
              "ActorId": "actor-1",
              "ActorRef": "ref",
              "NodeRid": "01",
              "Generation": 1,
              "LocationKind": 2,
              "SpotRid": "02",
              "SpotKind": 2,
              "OwnerId": "owner-a",
              "UpdatedAt": "2026-07-03T00:00:00Z"
            }
            """;

        ZLinkActorLocation row = ZLinkRedisLocationRowJson.deserializeActor(json, 5, UPDATED_AT.plusSeconds(2));

        assertEquals("game", row.actorType());
        assertEquals(ZLinkSpotKind.USER, row.locationKind());
        assertNull(row.spotMeshName());
        assertEquals(5, row.generation());
        assertEquals(UPDATED_AT.plusSeconds(2), row.updatedAt());
    }
}
