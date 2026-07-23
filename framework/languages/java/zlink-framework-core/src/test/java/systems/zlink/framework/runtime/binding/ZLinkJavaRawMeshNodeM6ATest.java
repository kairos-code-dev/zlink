package systems.zlink.framework.runtime.binding;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.RecordKind;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.runtime.internal.backend.ZLinkMeshDispatchRecord;
import systems.zlink.framework.runtime.backend.ZLinkBackendReceived;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;

final class ZLinkJavaRawMeshNodeM6ATest {
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
}
