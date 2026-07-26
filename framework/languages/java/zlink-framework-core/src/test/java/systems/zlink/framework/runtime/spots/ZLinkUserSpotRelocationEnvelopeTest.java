package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

final class ZLinkUserSpotRelocationEnvelopeTest {
    @Test
    void canonicalEnvelopeRoundTripsParticipantsTimersAndJournal() {
        RoutingId source = RoutingId.from("source-node");
        RoutingId target = RoutingId.from("target-node");
        LinkedHashMap<String, List<ZLinkAsyncSerialQueue.QueuedRecord>> journal =
            new LinkedHashMap<>();
        journal.put("spot", List.of(
            new ZLinkAsyncSerialQueue.QueuedRecord(3, new byte[] {3}),
            new ZLinkAsyncSerialQueue.QueuedRecord(4, new byte[] {4})));
        journal.put("actor:actor-a", List.of(
            new ZLinkAsyncSerialQueue.QueuedRecord(7, new byte[] {7})));
        var request = new ZLinkUserSpotAggregateStagingOwner.Request(
            TestSpot.class,
            "room",
            "room-a",
            5,
            new byte[] {1, 2},
            true,
            new byte[] {8, 9},
            List.of(new ZLinkUserSpotAggregateStagingOwner.ActorParticipant(
                "actor-a",
                "player",
                new byte[] {6},
                true,
                new ZLinkBackendActorRef(source, "actor-a", 11))),
            journal);

        var decoded = ZLinkUserSpotRelocationEnvelope.decode(
            ZLinkUserSpotRelocationEnvelope.encode(request),
            target,
            stableType -> stableType.equals("room") ? TestSpot.class : null);

        assertEquals("room-a", decoded.spotId());
        assertEquals(5, decoded.objectGeneration());
        assertArrayEquals(new byte[] {1, 2}, decoded.spotState());
        assertArrayEquals(new byte[] {8, 9}, decoded.timerEnvelope());
        assertEquals(target, decoded.actors().getFirst()
            .preparedActorRef().nodeRid());
        assertEquals(11, decoded.actors().getFirst()
            .preparedActorRef().generation());
        assertEquals(7, decoded.acceptedJournal()
            .get("actor:actor-a").getFirst().sequence());
    }

    @Test
    void unknownStableTypeAndTrailingBytesAreRejected() {
        RoutingId node = RoutingId.from("node");
        var request = new ZLinkUserSpotAggregateStagingOwner.Request(
            TestSpot.class,
            "room",
            "room-a",
            1,
            new byte[0],
            false,
            new byte[0],
            List.of(),
            java.util.Map.of());
        byte[] encoded = ZLinkUserSpotRelocationEnvelope.encode(request);

        assertThrows(
            IllegalArgumentException.class,
            () -> ZLinkUserSpotRelocationEnvelope.decode(
                encoded,
                node,
                ignored -> null));
        assertThrows(
            IllegalArgumentException.class,
            () -> ZLinkUserSpotRelocationEnvelope.decode(
                java.util.Arrays.copyOf(encoded, encoded.length + 1),
                node,
                ignored -> TestSpot.class));
    }

    private static final class TestSpot implements ZLinkSpot<ZLinkActor> {
        @Override public ZLinkSpotContext context() { return null; }

        @Override public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }

        @Override public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
            return CompletableFuture.completedFuture(null);
        }
    }
}
