package systems.zlink.framework.locations.redis;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkActorLocationKey;
import systems.zlink.framework.locations.ZLinkLocationAutoConnectType;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkPeerLocationKey;
import systems.zlink.framework.locations.ZLinkRouteKind;
import systems.zlink.framework.locations.ZLinkRouteLocationKey;
import systems.zlink.framework.locations.ZLinkSpotLocationKey;
import systems.zlink.framework.locations.ZLinkCreationOperationIdentity;

class ZLinkRedisLocationKeyCodecTest {
    @Test
    void peerKeyUsesCanonicalNamesAndRoutingIdHex() {
        String key = ZLinkRedisLocationKeyCodec.encodePeerKey(new ZLinkPeerLocationKey(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "mesh",
            ZLinkLocationRole.ROUTER,
            RoutingId.from(new byte[] {0x01, 0x23}),
            "tcp://ignored"));

        assertEquals("10:route-mesh4:mesh6:router4:0123", key);
    }

    @Test
    void peerKeyFallsBackToEndpointWhenRoutingIdIsAbsent() {
        String key = ZLinkRedisLocationKeyCodec.encodePeerKey(new ZLinkPeerLocationKey(
            ZLinkLocationAutoConnectType.FANOUT,
            "events",
            ZLinkLocationRole.PUB,
            null,
            "tcp://127.0.0.1:7000"));

        assertEquals("6:fanout6:events3:pub20:tcp://127.0.0.1:7000", key);
    }

    @Test
    void spotRowKeyUsesGlobalSpotIdNamespace() {
        assertEquals("4:0a0b", ZLinkRedisLocationKeyCodec.encodeSpotKey(
            new ZLinkSpotLocationKey("0a0b")));
        assertEquals("5:actor", ZLinkRedisLocationKeyCodec.encodeActorKey(
            new ZLinkActorLocationKey("actor")));
        assertEquals("1:19:route-key", ZLinkRedisLocationKeyCodec.encodeRouteKey(
            new ZLinkRouteLocationKey(ZLinkRouteKind.ACTOR_SESSION, "route-key")));
    }

    @Test
    void creationTerminalKeyUsesExactSourceLifecycleAndOperationId() {
        ZLinkRedisLocationKeys keys = new ZLinkRedisLocationKeys("P");

        assertEquals(
            "P:{zlink-location-v3}:creation-terminal:"
                + "2:0a0b:7:"
                + "00000000000000010000000000000002",
            keys.creationTerminalKey(
                new ZLinkCreationOperationIdentity(
                    RoutingId.from(new byte[] {0x0a, 0x0b}),
                    7,
                    1,
                    2)));
    }
}
