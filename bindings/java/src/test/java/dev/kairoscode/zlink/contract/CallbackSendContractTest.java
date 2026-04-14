/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.RequestResult;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.StreamSocket;
import dev.kairoscode.zlink.TestSupport;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Regression tests for send operations inside receive callbacks.
 *
 * <p>Blocking send APIs are explicitly rejected from callback context. Callers
 * that need callback-safe send behavior must use the explicit {@code try*}
 * APIs instead of relying on an implicit blocking-mode downgrade.
 */
public class CallbackSendContractTest {

    @Test
    public void routerSendInsideOnReceiveDoesNotHang() throws Exception {
        TestSupport.assumeNative();

        CountDownLatch replyReceived = new CountDownLatch(1);
        AtomicReference<byte[]> replyPayload = new AtomicReference<>();
        AtomicReference<Throwable> callbackError = new AtomicReference<>();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             DealerSocket dealer = new DealerSocket(ctx)) {

            String endpoint = TestSupport.tcpEndpoint();
            try (var routerMon = router.monitorOpen(MonitorEventType.CONNECTION_READY);
                 var dealerMon = dealer.monitorOpen(MonitorEventType.CONNECTION_READY)) {
                dealer.setRoutingId(RoutingId.fromBytes(
                    "request-reply-client".getBytes(StandardCharsets.UTF_8)));
                router.bind(endpoint);
                dealer.connect(endpoint);
                routerMon.recv();
                dealerMon.recv();
            }
            TestSupport.allowTcpRequestReplyCallbackHandshakeToSettle();

            // Install callback AFTER bind+connect+waitConnected so the
            // peer routing-id handshake completes through the normal
            // code path (xattach_pipe without dispatch mode active).
            router.onReceive(received -> {
                try {
                    RoutingId rid = received.routingId().orElseThrow();
                    assertNotNull(rid,
                        "router must receive routing id from dealer");
                    try (Message reply = Message.copyOfUtf8("pong")) {
                        received.reply(reply);
                    }
                } catch (Throwable t) {
                    callbackError.set(t);
                }
            });

            try (Message request = Message.copyOfUtf8("ping")) {
                dealer.request(request,
                    (result, received) -> {
                        try {
                            assertEquals(RequestResult.OK, result);
                            replyPayload.set(received.get(0).toByteArray());
                        } catch (Throwable t) {
                            callbackError.set(t);
                        } finally {
                            Message.closeAll(received);
                            replyReceived.countDown();
                        }
                    }, SendFlags.NONE,
                    Duration.ofMillis(TestSupport.DEFAULT_TIMEOUT_MS));
            }

            assertTrue(replyReceived.await(
                    TestSupport.DEFAULT_TIMEOUT_MS, TimeUnit.MILLISECONDS),
                "callback+send timed out -- send from callback hung");
            assertNull(callbackError.get(),
                "callback raised: " + callbackError.get());
            assertArrayEquals("pong".getBytes(StandardCharsets.UTF_8),
                replyPayload.get());
        }
    }

    @Test
    public void pairSendInsideOnReceiveDoesNotHang() throws Exception {
        TestSupport.assumeNative();

        CountDownLatch replyReceived = new CountDownLatch(1);
        AtomicReference<byte[]> replyPayload = new AtomicReference<>();
        AtomicReference<Throwable> callbackError = new AtomicReference<>();

        try (Context ctx = new Context();
             PairSocket left = new PairSocket(ctx);
             PairSocket right = new PairSocket(ctx)) {

            String endpoint = TestSupport.tcpEndpoint();
            try (var leftMon = left.monitorOpen(MonitorEventType.CONNECTION_READY);
                 var rightMon = right.monitorOpen(MonitorEventType.CONNECTION_READY)) {
                left.bind(endpoint);
                right.connect(endpoint);
                leftMon.recv();
                rightMon.recv();
            }

            // Right receives a message and sends a reply from the callback.
            right.onReceive(received -> {
                try {
                    byte[] data = received.singlePartOrThrow().toByteArray();
                    assertEquals("ping",
                        new String(data, StandardCharsets.UTF_8));
                    try (Message reply = Message.copyOfUtf8("pong")) {
                        right.send(reply, SendFlags.DONT_WAIT);
                    }
                } catch (Throwable t) {
                    callbackError.set(t);
                }
            });

            // Install the reply callback before sending so this test only
            // exercises callback-send behavior, not callback registration races.
            left.onReceive(received -> {
                try {
                    replyPayload.set(
                        received.singlePartOrThrow().toByteArray());
                } catch (Throwable t) {
                    callbackError.set(t);
                } finally {
                    replyReceived.countDown();
                }
            });

            try (Message request = Message.copyOfUtf8("ping")) {
                left.send(request);
            }

            assertTrue(replyReceived.await(
                    TestSupport.DEFAULT_TIMEOUT_MS, TimeUnit.MILLISECONDS),
                "callback+send timed out -- send from callback hung");
            assertNull(callbackError.get(),
                "callback raised: " + callbackError.get());
            assertArrayEquals("pong".getBytes(StandardCharsets.UTF_8),
                replyPayload.get());
        }
    }

    @Test
    public void routerMultiRoundCallbackSend() throws Exception {
        TestSupport.assumeNative();

        int roundCount = 5;
        CountDownLatch allReplies = new CountDownLatch(roundCount);
        AtomicReference<Throwable> callbackError = new AtomicReference<>();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx);
             DealerSocket dealer = new DealerSocket(ctx)) {

            String endpoint = TestSupport.tcpEndpoint();
            try (var routerMon = router.monitorOpen(MonitorEventType.CONNECTION_READY);
                 var dealerMon = dealer.monitorOpen(MonitorEventType.CONNECTION_READY)) {
                dealer.setRoutingId(RoutingId.fromBytes(
                    "request-reply-client".getBytes(StandardCharsets.UTF_8)));
                router.bind(endpoint);
                dealer.connect(endpoint);
                routerMon.recv();
                dealerMon.recv();
            }
            TestSupport.allowTcpRequestReplyCallbackHandshakeToSettle();

            // Install callback AFTER connection readiness confirmed.
            router.onReceive(received -> {
                try {
                    RoutingId rid = received.routingId().orElseThrow();
                    byte[] data = received.singlePartOrThrow().toByteArray();
                    String payload = new String(data, StandardCharsets.UTF_8);
                    assertTrue(payload.startsWith("request-"));
                    String index = payload.substring("request-".length());
                    try (Message reply =
                             Message.copyOfUtf8("reply-" + index)) {
                        received.reply(reply);
                    }
                } catch (Throwable t) {
                    if (callbackError.get() == null)
                        callbackError.set(t);
                }
            });

            for (int i = 0; i < roundCount; i++) {
                int index = i;
                try (Message request = Message.copyOfUtf8("request-" + i)) {
                    dealer.request(request,
                        (result, received) -> {
                            try {
                                assertEquals(RequestResult.OK, result);
                                byte[] data = received.get(0).toByteArray();
                                String payload = new String(data, StandardCharsets.UTF_8);
                                assertTrue(payload.startsWith("reply-"));
                                assertEquals("reply-" + index,
                                    payload);
                            } catch (Throwable t) {
                                if (callbackError.get() == null)
                                    callbackError.set(t);
                            } finally {
                                Message.closeAll(received);
                                allReplies.countDown();
                            }
                        }, SendFlags.NONE,
                        Duration.ofMillis(TestSupport.DEFAULT_TIMEOUT_MS));
                }
            }

            assertTrue(allReplies.await(
                    TestSupport.DEFAULT_TIMEOUT_MS, TimeUnit.MILLISECONDS),
                "multi-round callback+send timed out");
            assertNull(callbackError.get(),
                "callback raised: " + callbackError.get());
        }
    }

    @Test
    public void streamRawCallbackSendDoesNotCopyThroughReceivedSurface()
        throws Exception {
        TestSupport.assumeNative();

        CountDownLatch echoed = new CountDownLatch(1);
        AtomicReference<Throwable> callbackError = new AtomicReference<>();
        byte[] outbound = "ping".getBytes(StandardCharsets.UTF_8);
        String endpoint = TestSupport.tcpEndpoint();
        int port = Integer.parseInt(endpoint.substring(endpoint.lastIndexOf(':')
            + 1));

        try (Context ctx = new Context();
             StreamSocket server = new StreamSocket(ctx)) {
            server.options().notify(true);
            server.bind(endpoint);
            server.attachStreamRaw((routingId, payload) -> {
                try {
                    if (routingId == null)
                        return 0;
                    int size = payload.size();
                    if (size == 0)
                        return 0;
                    if (size == 1) {
                        int marker = payload.dataBuffer().get(0) & 0xFF;
                        if (marker == 0x00 || marker == 0x01)
                            return 0;
                    }
                    server.send(routingId, payload, SendFlags.DONT_WAIT);
                    echoed.countDown();
                    return 0;
                } catch (Throwable t) {
                    callbackError.compareAndSet(null, t);
                    return 1;
                }
            });

            try (java.net.Socket client = new java.net.Socket("127.0.0.1", port)) {
                client.setSoTimeout(TestSupport.DEFAULT_TIMEOUT_MS);
                client.getOutputStream().write(outbound);
                client.getOutputStream().flush();
                byte[] echoedPayload = client.getInputStream().readNBytes(
                    outbound.length);
                assertArrayEquals(outbound, echoedPayload);
            }

            assertTrue(echoed.await(TestSupport.DEFAULT_TIMEOUT_MS,
                    TimeUnit.MILLISECONDS),
                "stream raw callback echo timed out");
            assertNull(callbackError.get(),
                "callback raised: " + callbackError.get());
        }
    }

    @Test
    public void blockingSendInsideOnReceiveIsRejectedExplicitly() throws Exception {
        TestSupport.assumeNative();

        CountDownLatch callbackObserved = new CountDownLatch(1);
        AtomicReference<Throwable> callbackError = new AtomicReference<>();
        AtomicReference<String> rejectionMessage = new AtomicReference<>();

        try (Context ctx = new Context();
             PairSocket left = new PairSocket(ctx);
             PairSocket right = new PairSocket(ctx)) {

            String endpoint = TestSupport.tcpEndpoint();
            try (var leftMon = left.monitorOpen(MonitorEventType.CONNECTION_READY);
                 var rightMon = right.monitorOpen(MonitorEventType.CONNECTION_READY)) {
                left.bind(endpoint);
                right.connect(endpoint);
                leftMon.recv();
                rightMon.recv();
            }

            right.onReceive(received -> {
                try {
                    try (Message reply = Message.copyOfUtf8("pong")) {
                        try {
                            right.send(reply);
                            throw new IllegalStateException(
                                "blocking send in callback must be rejected");
                        } catch (IllegalStateException ex) {
                            rejectionMessage.set(ex.getMessage());
                        }
                    }
                } catch (Throwable t) {
                    callbackError.set(t);
                } finally {
                    callbackObserved.countDown();
                }
            });

            try (Message request = Message.copyOfUtf8("ping")) {
                left.send(request);
            }

            assertTrue(callbackObserved.await(
                    TestSupport.DEFAULT_TIMEOUT_MS, TimeUnit.MILLISECONDS),
                "callback rejection timed out");
            assertNull(callbackError.get(),
                "callback raised: " + callbackError.get());
            assertNotNull(rejectionMessage.get());
            assertTrue(rejectionMessage.get().contains("blocking send"));
            assertTrue(rejectionMessage.get().contains("callback context"));
        }
    }
}
