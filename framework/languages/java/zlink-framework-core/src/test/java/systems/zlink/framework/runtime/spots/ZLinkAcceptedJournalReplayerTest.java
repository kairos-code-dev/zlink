package systems.zlink.framework.runtime.spots;

import static org.junit.jupiter.api.Assertions.*;

import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.execution.ZLinkAsyncSerialQueue;
import systems.zlink.framework.runtime.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

final class ZLinkAcceptedJournalReplayerTest {
    @Test
    void decodesSpotRequestAndRelaysReplyAfterDispatch() {
        List<String> order = new ArrayList<>();
        byte[] encoded;
        try (var received = new ZLinkBackendReceived(
            Optional.of(RoutingId.from("source")),
            Optional.of("room-a"),
            Optional.of(17L),
            List.of(Message.from(new byte[] {1})))) {
            encoded = ZLinkSpotAcceptedJournal.encode(received);
        }
        var replayer = new ZLinkAcceptedJournalReplayer(
            request -> {
                order.add("dispatch");
                return CompletableFuture.completedFuture(
                    List.of(new byte[] {9}));
            },
            request -> CompletableFuture.completedFuture(Optional.empty()),
            new ZLinkAcceptedJournalReplayer.ReplyRelay() {
                @Override public java.util.concurrent.CompletionStage<Void>
                    relaySpot(
                        ZLinkSpotAcceptedJournal.Record request,
                        List<byte[]> reply) {
                    order.add("reply:" + request.requestSequence().orElseThrow());
                    assertArrayEquals(new byte[] {9}, reply.get(0));
                    return CompletableFuture.completedFuture(null);
                }
                @Override public java.util.concurrent.CompletionStage<Void>
                    relayActor(
                        ZLinkActorAcceptedJournal.Record request,
                        byte[] reply) {
                    return CompletableFuture.failedFuture(
                        new AssertionError("unexpected Actor reply"));
                }
            });

        replayer.replay("spot", new ZLinkAsyncSerialQueue.QueuedRecord(
            1, encoded)).toCompletableFuture().join();

        assertEquals(List.of("dispatch", "reply:17"), order);
    }

    @Test
    void actorLaneMustMatchEncodedActorAndPreservesHeader() {
        var header = new ZLinkStreamHeader(
            ZLinkStreamMessageKind.REQUEST,
            ZLinkStreamCodec.RAW,
            EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
            Optional.of(41L),
            "probe",
            Map.of());
        byte[] encoded;
        try (Message payload = Message.from(new byte[] {3})) {
            encoded = ZLinkActorAcceptedJournal.encode(
                "actor-a", header, payload);
        }
        var replayer = new ZLinkAcceptedJournalReplayer(
            request -> CompletableFuture.completedFuture(List.of()),
            request -> {
                assertEquals("actor-a", request.actorId());
                assertEquals(
                    41L,
                    request.header().requestSequence().orElseThrow());
                return CompletableFuture.completedFuture(
                    Optional.of(new byte[] {7}));
            },
            new ZLinkAcceptedJournalReplayer.ReplyRelay() {
                @Override public java.util.concurrent.CompletionStage<Void>
                    relaySpot(
                        ZLinkSpotAcceptedJournal.Record request,
                        List<byte[]> reply) {
                    return CompletableFuture.failedFuture(
                        new AssertionError("unexpected Spot reply"));
                }
                @Override public java.util.concurrent.CompletionStage<Void>
                    relayActor(
                        ZLinkActorAcceptedJournal.Record request,
                        byte[] reply) {
                    assertArrayEquals(new byte[] {7}, reply);
                    return CompletableFuture.completedFuture(null);
                }
            });

        replayer.replay("actor:actor-a",
            new ZLinkAsyncSerialQueue.QueuedRecord(1, encoded))
            .toCompletableFuture().join();

        assertThrows(
            java.util.concurrent.CompletionException.class,
            () -> replayer.replay("actor:actor-b",
                    new ZLinkAsyncSerialQueue.QueuedRecord(1, encoded))
                .toCompletableFuture().join());
    }
}
