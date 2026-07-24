package systems.zlink.framework.runtime.locations;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;

final class ZLinkServiceAuthorityPayloadCodecTest {
    @Test
    void readyUserSpotRoundTripsIntoDurableRouteFields() {
        var codec = new ZLinkServiceAuthorityPayloadCodec();
        RoutingId spotRid = RoutingId.from("spot-17");
        RoutingId nodeRid = RoutingId.from("node-b");

        byte[] payload = codec.encodeUser(
            ZLinkServiceAuthorityPayloadCodec.State.READY,
            "game.player",
            spotRid,
            "owner-b",
            31,
            "game",
            nodeRid,
            17);

        var decoded = codec.decode(payload).orElseThrow();
        assertEquals(ZLinkServiceAuthorityPayloadCodec.Kind.USER, decoded.kind());
        assertEquals(ZLinkServiceAuthorityPayloadCodec.State.READY, decoded.state());
        assertEquals("game.player", decoded.stableType());
        assertEquals(spotRid, decoded.spotRid());
        assertEquals("owner-b", decoded.ownerId());
        assertEquals(31, decoded.ownerLeaseGeneration());
        assertEquals("game", decoded.meshName());
        assertEquals(nodeRid, decoded.nodeRid());
        assertEquals(17, decoded.nodeGeneration());
    }

    @Test
    void corruptedAuthorityNeverEntersDurableRouteCache() {
        var codec = new ZLinkServiceAuthorityPayloadCodec();
        byte[] payload = codec.encodeUser(
            ZLinkServiceAuthorityPayloadCodec.State.CREATING,
            "game.player",
            RoutingId.from("spot-17"),
            "owner-b",
            31,
            "game",
            RoutingId.from("node-b"),
            17);
        payload[payload.length - 1] ^= 1;

        assertTrue(codec.decode(payload).isEmpty());
    }

    @Test
    void spotAuthorityKeyUsesCanonicalLengthAndEscaping() {
        assertEquals(
            "zla1:s:4:a%3Ab%00",
            ZLinkAuthorityKeyCodec.spot(
                RoutingId.from(new byte[] {'a', ':', 'b', 0})));
    }
}
