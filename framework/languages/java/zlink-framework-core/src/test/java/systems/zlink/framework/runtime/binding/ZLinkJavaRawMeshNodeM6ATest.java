package systems.zlink.framework.runtime.binding;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.internal.binding.spot.RecordKind;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.internal.backend.ZLinkBackendRequestResult;
import systems.zlink.framework.runtime.internal.service.ZLinkServiceM6BWireCodec;

final class ZLinkJavaRawMeshNodeM6ATest {
    @Test
    void command44ReceivesCommand45ThroughInfrastructureDispatcher()
        throws Exception {
        String endpoint = "inproc://jvm-m6c-session-route-" + System.nanoTime();
        RoutingId sourceRid = RoutingId.from("jvm-m6c-target-owner");
        RoutingId sessionOwnerRid = RoutingId.from("jvm-m6c-session-owner");
        RoutingId sessionRid = RoutingId.from("jvm-m6c-session");
        var codec = new ZLinkServiceM6BWireCodec();
        var command = new ZLinkServiceM6BWireCodec.SessionRelocationRoute(
            new ZLinkServiceM6BWireCodec.RelocationIdentity(1, 2),
            new ZLinkServiceM6BWireCodec.RelocationCoordinatorFence(
                "coordinator", 3, sourceRid, 4, "store-v5"),
            ZLinkServiceM6BWireCodec.RelocationRole.TARGET,
            new ZLinkServiceM6BWireCodec.ActorIdentity("actor", 6),
            new ZLinkServiceM6BWireCodec.SessionOwnerFence(
                sessionOwnerRid, 7, "session-owner", 8, sessionRid, 9),
            ZLinkServiceM6BWireCodec.SessionRelocationRouteAction.COMMIT,
            10, 11, sourceRid, 12, 17);
        try (var context = Zlink.createContext();
             var source = new ZLinkJavaRawMeshNode(context, "mesh");
             var sessionOwner = new ZLinkJavaRawMeshNode(context, "mesh")) {
            source.setRoutingId(sourceRid);
            source.setBind("inproc://jvm-m6c-target-owner-" + System.nanoTime());
            sessionOwner.setRoutingId(sessionOwnerRid);
            sessionOwner.setBind(endpoint);
            AtomicInteger applicationDispatches = new AtomicInteger();
            sessionOwner.startDispatch(record -> {
                applicationDispatches.incrementAndGet();
                record.close();
            });
            sessionOwner.setSessionRelocationRouteHandler((actualSource, encoded) -> {
                assertEquals(sourceRid, actualSource);
                assertEquals(command, codec.decodeSessionRelocationRoute(encoded));
                return CompletableFuture.completedFuture(
                    codec.encodeSessionRelocationRouted(
                        new ZLinkServiceM6BWireCodec.SessionRelocationRouted(
                            command.relocation(), command.coordinator(),
                            command.actor(), command.session(), command.action(),
                            command.currentAuthorityOwnerGeneration(),
                            command.lastAcceptedSessionSequence())));
            });
            source.start();
            sessionOwner.start();
            source.connectPeer(endpoint, sessionOwnerRid);
            awaitAdmitted(source);

            var ack = codec.decodeSessionRelocationRouted(
                source.requestSessionRelocationRoute(
                        sessionOwnerRid,
                        codec.encodeSessionRelocationRoute(command),
                        Duration.ofSeconds(2))
                    .toCompletableFuture().get(2, TimeUnit.SECONDS));

            assertEquals(command.relocation(), ack.relocation());
            assertEquals(17, ack.lastAcceptedSessionSequence());
            assertEquals(0, applicationDispatches.get());
        }
    }

    @Test
    void relocationControlUsesAdmittedPeerAndBypassesApplicationMailbox()
        throws Exception {
        String endpoint = "inproc://jvm-m6c-relocation-" + System.nanoTime();
        RoutingId sourceRid = RoutingId.from("jvm-m6c-relocation-source");
        RoutingId targetRid = RoutingId.from("jvm-m6c-relocation-target");
        try (var context = Zlink.createContext();
             var source = new ZLinkJavaRawMeshNode(context, "mesh");
             var target = new ZLinkJavaRawMeshNode(context, "mesh")) {
            source.setRoutingId(sourceRid);
            source.setBind("inproc://jvm-m6c-relocation-source-"
                + System.nanoTime());
            target.setRoutingId(targetRid);
            target.setBind(endpoint);
            AtomicInteger applicationDispatches = new AtomicInteger();
            target.startDispatch(record -> {
                applicationDispatches.incrementAndGet();
                record.close();
            });
            target.setRelocationControlHandler((actualSource, command) -> {
                assertEquals(sourceRid, actualSource);
                assertArrayEquals(new byte[] {1, 2, 3}, command);
                return CompletableFuture.completedFuture(
                    new byte[] {9, 8, 7});
            });
            source.start();
            target.start();
            source.connectPeer(endpoint, targetRid);
            awaitAdmitted(source);

            byte[] reply = source.requestRelocationControl(
                    targetRid,
                    new byte[] {1, 2, 3},
                    Duration.ofSeconds(2))
                .toCompletableFuture()
                .get(2, TimeUnit.SECONDS);

            assertArrayEquals(new byte[] {9, 8, 7}, reply);
            assertEquals(0, applicationDispatches.get());
        }
    }

    @Test
    void nodeSendUsesOnlyRawPublicBindingAndDispatchesOwnedParts() throws Exception {
        String endpoint = "inproc://jvm-m6a-" + System.nanoTime();
        RoutingId leftRid = RoutingId.from("jvm-m6a-left");
        RoutingId rightRid = RoutingId.from("jvm-m6a-right");
        try (var context = Zlink.createContext();
             var left = new ZLinkJavaRawMeshNode(context, "mesh");
             var right = new ZLinkJavaRawMeshNode(context, "mesh")) {
            left.setRoutingId(leftRid);
            left.setBind(endpoint);
            right.setRoutingId(rightRid);
            right.setBind("inproc://jvm-m6a-right-" + System.nanoTime());
            left.start();
            right.start();
            right.connectPeer(endpoint, leftRid);

            CompletableFuture<ZLinkMeshDispatchRecord> received =
                new CompletableFuture<>();
            left.startDispatch(received::complete);
            Message packet = Message.from("packet");
            Message payload = Message.from(new byte[] {1, 2, 3});
            try {
                long deadline =
                    System.nanoTime() + Duration.ofSeconds(2).toNanos();
                boolean submitted = false;
                while (!submitted && System.nanoTime() < deadline) {
                    submitted = right.spotNode().sendToNode(
                        leftRid,
                        List.of(packet, payload),
                        SendFlags.DONT_WAIT);
                    if (!submitted) {
                        Thread.sleep(1);
                    }
                }
                assertTrue(submitted);
            } finally {
                packet.close();
                payload.close();
            }

            try (ZLinkMeshDispatchRecord record =
                received.get(2, TimeUnit.SECONDS)) {
                assertEquals(RecordKind.NODE_SEND, record.receive().kind());
                assertEquals(rightRid, record.receive().sourceNodeRid());
                assertEquals("packet", record.parts().getFirst().toUtf8String());
                assertArrayEquals(
                    new byte[] {1, 2, 3},
                    record.parts().get(1).toByteArray());
            }
        }
    }

    @Test
    void nodeRequestCompletesExactlyOnceThroughFrameworkReply() throws Exception {
        String endpoint = "inproc://jvm-m6a-request-" + System.nanoTime();
        RoutingId leftRid = RoutingId.from("jvm-m6a-request-left");
        RoutingId rightRid = RoutingId.from("jvm-m6a-request-right");
        try (var context = Zlink.createContext();
             var left = new ZLinkJavaRawMeshNode(context, "mesh");
             var right = new ZLinkJavaRawMeshNode(context, "mesh")) {
            left.setRoutingId(leftRid);
            left.setBind(endpoint);
            right.setRoutingId(rightRid);
            right.setBind("inproc://jvm-m6a-request-right-" + System.nanoTime());
            left.start();
            right.start();
            right.connectPeer(endpoint, leftRid);
            left.startDispatch(record -> {
                try (record;
                     Message packet = Message.from("reply");
                     Message payload = Message.from(new byte[] {9, 8, 7})) {
                    assertEquals(RecordKind.NODE_REQUEST, record.receive().kind());
                    record.reply(List.of(packet, payload));
                }
            });

            CompletableFuture<ZLinkBackendReceived> completed =
                new CompletableFuture<>();
            try (Message packet = Message.from("request");
                 Message payload = Message.from(new byte[] {1})) {
                long deadline =
                    System.nanoTime() + Duration.ofSeconds(2).toNanos();
                boolean submitted = false;
                while (!submitted && System.nanoTime() < deadline) {
                    submitted = right.spotNode().requestToNode(
                        leftRid,
                        List.of(packet, payload),
                        completed::complete,
                        SendFlags.DONT_WAIT,
                        Duration.ofSeconds(2));
                    if (!submitted) {
                        Thread.sleep(1);
                    }
                }
                assertTrue(submitted);
            }

            try (ZLinkBackendReceived reply =
                completed.get(2, TimeUnit.SECONDS)) {
                assertEquals(ZLinkBackendRequestResult.OK, reply.result());
                assertEquals(1, reply.parts().size());
                assertArrayEquals(
                    new byte[] {9, 8, 7},
                    reply.parts().getFirst().toByteArray());
            }
        }
    }

    private static void awaitAdmitted(ZLinkJavaRawMeshNode node)
        throws InterruptedException {
        long deadline =
            System.nanoTime() + Duration.ofSeconds(2).toNanos();
        while (node.peers().stream().noneMatch(peer ->
                peer.state()
                    == systems.zlink.framework.runtime.internal.binding.spot
                        .MeshPeerState.ADMITTED)
            && System.nanoTime() < deadline) {
            Thread.sleep(1);
        }
        assertTrue(node.peers().stream().anyMatch(peer ->
            peer.state()
                == systems.zlink.framework.runtime.internal.binding.spot
                    .MeshPeerState.ADMITTED));
    }
}
