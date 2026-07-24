package systems.zlink.framework.runtime.service;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Arrays;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;

final class ZLinkServiceM6BBoundSessionWireCodecTest {
    private final ZLinkServiceM6BWireCodec codec =
        new ZLinkServiceM6BWireCodec();

    @Test
    void command36RoundTripsTheCompleteActorAndBindingFence() {
        var route = route();
        byte[] encoded =
            codec.encodeBoundSessionSendHeader(route, 19);
        var decoded =
            codec.decodeBoundSessionSendHeader(encoded);

        assertEquals(route, decoded.actor());
        assertEquals(19, decoded.expectedBindingGeneration());
        assertArrayEquals(
            encoded,
            codec.encodeBoundSessionSendHeader(
                decoded.actor(),
                decoded.expectedBindingGeneration()));
    }

    @Test
    void command38RoundTripsActiveAndTombstoneTransitions() {
        var active = new ZLinkServiceM6BWireCodec.BoundSessionBind(
            7,
            route(),
            RoutingId.from("session"),
            true,
            23);
        var tombstone =
            new ZLinkServiceM6BWireCodec.BoundSessionBind(
                8,
                route(),
                RoutingId.from("session"),
                false,
                23);

        var decodedActive = codec.decodeBoundSessionBindHeader(
            codec.encodeBoundSessionBindHeader(active));
        var decodedTombstone = codec.decodeBoundSessionBindHeader(
            codec.encodeBoundSessionBindHeader(tombstone));

        assertTrue(decodedActive.active());
        assertFalse(decodedTombstone.active());
        assertEquals(active, decodedActive);
        assertEquals(tombstone, decodedTombstone);
    }

    @Test
    void boundActorTailRequiresPairedFlagsAndRejectsMalformedFrames() {
        int flags =
            systems.zlink.framework.runtime.protocol.ServiceWireConstants
                .FLAG_BOUND_SESSION
                | systems.zlink.framework.runtime.protocol
                    .ServiceWireConstants.FLAG_SOURCE_SPOT_ID;
        var tail = new ZLinkServiceM6BWireCodec.BoundSessionTail(
            RoutingId.from("session"),
            3,
            4);
        byte[] actor = codec.encodeActorHeader(
            false,
            flags,
            null,
            null,
            route(),
            tail);
        assertEquals(
            tail,
            codec.decodeActorHeader(actor).boundSession());

        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.encodeActorHeader(
                false,
                systems.zlink.framework.runtime.protocol
                    .ServiceWireConstants.FLAG_BOUND_SESSION,
                null,
                null,
                route(),
                tail));

        byte[] invalidFlags =
            codec.encodeBoundSessionSendHeader(route(), 1);
        invalidFlags[4] = 1;
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.decodeBoundSessionSendHeader(invalidFlags));

        byte[] truncated = codec.encodeBoundSessionBindHeader(
            new ZLinkServiceM6BWireCodec.BoundSessionBind(
                1,
                route(),
                RoutingId.from("session"),
                true,
                1));
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.decodeBoundSessionBindHeader(
                Arrays.copyOf(truncated, truncated.length - 1)));

        byte[] trailing = Arrays.copyOf(truncated, truncated.length + 1);
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.decodeBoundSessionBindHeader(trailing));

        byte[] badUnionLength = truncated.clone();
        int lengthOffset = badUnionLength.length - Long.BYTES - 2;
        badUnionLength[lengthOffset + 1] = 7;
        assertThrows(
            ZLinkServiceWireException.class,
            () -> codec.decodeBoundSessionBindHeader(badUnionLength));
    }

    private static ZLinkServiceM6BWireCodec.ActorRouteFence route() {
        RoutingId node = RoutingId.from("actor-node");
        return new ZLinkServiceM6BWireCodec.ActorRouteFence(
            new ZLinkBackendActorRef(node, "actor", 11),
            13,
            17);
    }
}
