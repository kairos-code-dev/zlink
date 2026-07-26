package systems.zlink.framework.runtime.actors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.time.Duration;
import java.util.List;
import java.util.EnumSet;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

final class ZLinkActorTransferHandoffTest {
    @Test
    void inFlightHandoffKeepsArrivalOrder() {
        ZLinkActorTransferHandoff handoff = new ZLinkActorTransferHandoff();
        handoff.begin("actor");
        capture(handoff, "P1", Map.of());
        capture(handoff, "P2", Map.of());
        capture(handoff, "P3", Map.of());

        List<ZLinkActorHandoffPacket> backlog = handoff.take("actor");
        assertEquals(List.of("P1", "P2", "P3"),
            backlog.stream().map(packet -> packet.header().packetName()).toList());
        assertTrue(backlog.get(0).arrivalIndex() < backlog.get(1).arrivalIndex());
        assertTrue(backlog.get(1).arrivalIndex() < backlog.get(2).arrivalIndex());
        backlog.forEach(ZLinkActorHandoffPacket::close);
    }

    @Test
    void packetsAfterCommitSnapshotRemainBehindSnapshot() {
        ZLinkActorTransferHandoff handoff = new ZLinkActorTransferHandoff();
        handoff.begin("actor");
        capture(handoff, "B1", Map.of());
        capture(handoff, "B2", Map.of());
        List<ZLinkActorHandoffPacket> committed = handoff.take("actor");
        capture(handoff, "D1", Map.of());
        List<ZLinkActorHandoffPacket> trailing = handoff.finish("actor");

        assertEquals(List.of("B1", "B2", "D1"),
            java.util.stream.Stream.concat(committed.stream(), trailing.stream())
                .map(packet -> packet.header().packetName()).toList());
        committed.forEach(ZLinkActorHandoffPacket::close);
        trailing.forEach(ZLinkActorHandoffPacket::close);
    }

    @Test
    void boundSessionRouteMetadataSurvivesHandoff() {
        ZLinkActorTransferHandoff handoff = new ZLinkActorTransferHandoff();
        handoff.begin("actor");
        capture(handoff, "S1", Map.of("actor-id", "actor", "session", "bound-1"));

        ZLinkActorHandoffPacket packet = handoff.take("actor").get(0);
        assertEquals("actor", packet.header().metadata().get("actor-id"));
        assertEquals("bound-1", packet.header().metadata().get("session"));
        packet.close();
    }

    @Test
    void forwardingWindowEvictsMapping() throws Exception {
        ZLinkActorTransferHandoff handoff = new ZLinkActorTransferHandoff();
        CountDownLatch retired = new CountDownLatch(1);
        handoff.retain("actor", ref("source", 1), ref("target", 2),
            Duration.ofMillis(25), ignored -> retired.countDown());

        assertEquals(1, handoff.forwardingSourceCount());
        assertTrue(retired.await(1, TimeUnit.SECONDS));
        assertEquals(0, handoff.forwardingSourceCount());
        handoff.close();
    }

    @Test
    void repeatedMoveReplacesNodeMappingWithoutLeakingEntries() throws Exception {
        ZLinkActorTransferHandoff handoff = new ZLinkActorTransferHandoff();
        List<Long> retiredTargets = new CopyOnWriteArrayList<>();
        CountDownLatch retired = new CountDownLatch(2);
        handoff.retain("actor", ref("source", 1), ref("target", 2),
            Duration.ofMillis(30), source -> {
                retiredTargets.add(source.targetActorRef().generation());
                retired.countDown();
            });
        handoff.retain("actor", ref("source", 3), ref("target", 4),
            Duration.ofMillis(60), source -> {
                retiredTargets.add(source.targetActorRef().generation());
                retired.countDown();
            });

        assertEquals(1, handoff.forwardingSourceCount());
        assertEquals(4, handoff.forwardingSource("actor").orElseThrow()
            .targetActorRef().generation());
        assertTrue(retired.await(1, TimeUnit.SECONDS));
        assertEquals(List.of(2L, 4L), retiredTargets);
        assertEquals(0, handoff.forwardingSourceCount());
        handoff.close();
    }

    @Test
    void closeRetiresOwnedForwardingSourcesImmediately() {
        ZLinkActorTransferHandoff handoff = new ZLinkActorTransferHandoff();
        List<Long> retiredTargets = new CopyOnWriteArrayList<>();
        handoff.retain("actor", ref("source", 1), ref("target", 2),
            Duration.ofMinutes(1), source ->
                retiredTargets.add(source.targetActorRef().generation()));

        handoff.close();

        assertEquals(List.of(2L), retiredTargets);
        assertEquals(0, handoff.forwardingSourceCount());
    }

    @Test
    void inFlightRequestPreservesReplyCorrelationFraming() {
        ZLinkActorTransferHandoff handoff = new ZLinkActorTransferHandoff();
        handoff.begin("actor");
        ZLinkStreamHeader header = new ZLinkStreamHeader(
            ZLinkStreamMessageKind.REQUEST,
            ZLinkStreamCodec.JSON,
            EnumSet.of(ZLinkStreamHeaderFlag.HAS_METADATA),
            Optional.of(77L),
            "ProbeReq",
            Map.of("trace", "handoff"),
            Optional.of("corr-77"));
        ZLinkActorReplyRoute route = new ZLinkActorReplyRoute(
            ref("source", 9), RoutingId.from("caller-node"),
            RoutingId.from("caller-session"), 91L, 5);
        try (Message payload = Message.from(new byte[] {1})) {
            handoff.capture("actor", header, payload, route);
        }

        ZLinkActorHandoffPacket packet = handoff.take("actor").get(0);
        assertEquals(Optional.of(77L), packet.header().requestSequence());
        assertEquals(Optional.of("corr-77"), packet.header().correlationId());
        assertEquals("handoff", packet.header().metadata().get("trace"));
        assertEquals(route, packet.replyRoute());
        packet.close();
    }

    @Test
    void committedForwardingMappingBoundsMessagesAndBytes() {
        ZLinkActorTransferHandoff handoff = new ZLinkActorTransferHandoff();
        handoff.retain(
            "actor", ref("source", 7), ref("target", 7),
            Duration.ofMinutes(1), ignored -> { });
        List<CompletableFuture<Void>> pending = new java.util.ArrayList<>();
        for (int index = 0;
             index < ZLinkActorTransferHandoff.MAX_FORWARDING_MESSAGES;
             index++) {
            CompletableFuture<Void> operation = new CompletableFuture<>();
            pending.add(operation);
            handoff.forward("actor", 7, 1, () -> operation);
        }

        assertThrows(CompletionException.class, () ->
            handoff.forward(
                    "actor", 7, 1,
                    () -> CompletableFuture.completedFuture(null))
                .toCompletableFuture().join());
        pending.forEach(value -> value.complete(null));

        CompletableFuture<Void> bytes = new CompletableFuture<>();
        handoff.forward(
            "actor", 7, ZLinkActorTransferHandoff.MAX_FORWARDING_BYTES,
            () -> bytes);
        assertThrows(CompletionException.class, () ->
            handoff.forward(
                    "actor", 7, 1,
                    () -> CompletableFuture.completedFuture(null))
                .toCompletableFuture().join());
        bytes.complete(null);
        handoff.close();
    }

    @Test
    void forwardingRejectsDifferentObjectGeneration() {
        ZLinkActorTransferHandoff handoff = new ZLinkActorTransferHandoff();
        handoff.retain(
            "actor", ref("source", 7), ref("target", 7),
            Duration.ofMinutes(1), ignored -> { });

        assertThrows(CompletionException.class, () ->
            handoff.forward(
                    "actor", 8, 1,
                    () -> CompletableFuture.completedFuture(null))
                .toCompletableFuture().join());
        handoff.close();
    }

    private static void capture(
        ZLinkActorTransferHandoff handoff,
        String packetName,
        Map<String, String> metadata) {
        try (Message payload = Message.from(packetName.getBytes(java.nio.charset.StandardCharsets.UTF_8))) {
            handoff.capture(
                "actor",
                new ZLinkStreamHeader(packetName, metadata, Optional.empty()),
                payload,
                null);
        }
    }

    private static ZLinkBackendActorRef ref(String node, long generation) {
        return new ZLinkBackendActorRef(RoutingId.from(node), "actor", generation);
    }
}
