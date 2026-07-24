package systems.zlink.framework.runtime.locations;

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

class ZLinkLocationKeyCodecTest {
    @Test
    void encodesPeerKeyWithCanonicalNamesAndRoutingIdBytes() {
        RoutingId rid = RoutingId.from(new byte[] {0x0a, 0x2b});

        String encoded = ZLinkLocationKeyCodec.encodePeerKey(new ZLinkPeerLocationKey(
            ZLinkLocationAutoConnectType.ROUTE_MESH,
            "mesh-a",
            ZLinkLocationRole.ROUTER,
            rid,
            "tcp://ignored"));

        assertEquals("10:route-mesh6:mesh-a6:router4:0a2b", encoded);
    }

    @Test
    void encodesPeerKeyWithEndpointWhenRoutingIdIsAbsent() {
        String encoded = ZLinkLocationKeyCodec.encodePeerKey(new ZLinkPeerLocationKey(
            ZLinkLocationAutoConnectType.CLIENT_SERVER,
            "mesh-a",
            ZLinkLocationRole.DEALER,
            null,
            "tcp://127.0.0.1:6000"));

        assertEquals("13:client-server6:mesh-a6:dealer20:tcp://127.0.0.1:6000", encoded);
    }

    @Test
    void encodesSpotActorAndRouteKeys() {
        assertEquals(
            "4:0102",
            ZLinkLocationKeyCodec.encodeSpotKey(
                new ZLinkSpotLocationKey("0102")));
        assertEquals(
            new ZLinkSpotLocationKey("0102"),
            new ZLinkSpotLocationKey("0102"));
        assertEquals(
            "8:actor-42",
            ZLinkLocationKeyCodec.encodeActorKey(new ZLinkActorLocationKey("actor-42")));
        assertEquals(
            "1:19:route-key",
            ZLinkLocationKeyCodec.encodeRouteKey(
                new ZLinkRouteLocationKey(ZLinkRouteKind.ACTOR_SESSION, "route-key")));
    }
}
