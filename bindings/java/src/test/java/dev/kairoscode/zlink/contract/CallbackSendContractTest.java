/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.RequestDealer;
import dev.kairoscode.zlink.RequestRouter;
import dev.kairoscode.zlink.RequestResult;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.SendFlags;
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
             RequestRouter requestRouter = new RequestRouter(router);
             DealerSocket dealer = new DealerSocket(ctx);
             RequestDealer requestDealer = new RequestDealer(dealer)) {

            String endpoint = TestSupport.tcpEndpoint();
            try (var routerMon = router.monitorOpen(MonitorEventType.CONNECTION_READY);
                 var dealerMon = dealer.monitorOpen(MonitorEventType.CONNECTION_READY)) {
                dealer.setRoutingId(RoutingId.copyOf(
                    "request-reply-client".getBytes(StandardCharsets.UTF_8)));
                router.bind(endpoint);
                dealer.connect(endpoint);
                routerMon.recv();
                dealerMon.recv();
            }

            // Install callback AFTER bind+connect+waitConnected so the
            // peer routing-id handshake completes through the normal
            // code path (xattach_pipe without dispatch mode active).
            requestRouter.onReceive(received -> {
                try {
                    RoutingId rid = received.routingId();
                    assertNotNull(rid,
                        "router must receive routing id from dealer");
                    try (Message reply = Message.copyOfUtf8("pong")) {
                        requestRouter.reply(rid, received.requestSequence(),
                            reply, SendFlags.DONT_WAIT);
                    }
                } catch (Throwable t) {
                    callbackError.set(t);
                }
            });

            requestDealer.request(Message.copyOfUtf8("ping"),
                (result, received) -> {
                    try {
                        assertEquals(RequestResult.OK, result);
                        replyPayload.set(
                            received.singlePartOrThrow().toByteArray());
                    } catch (Throwable t) {
                        callbackError.set(t);
                    } finally {
                        replyReceived.countDown();
                    }
                }, SendFlags.NONE,
                Duration.ofMillis(TestSupport.DEFAULT_TIMEOUT_MS));

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
             RequestRouter requestRouter = new RequestRouter(router);
             DealerSocket dealer = new DealerSocket(ctx);
             RequestDealer requestDealer = new RequestDealer(dealer)) {

            String endpoint = TestSupport.tcpEndpoint();
            try (var routerMon = router.monitorOpen(MonitorEventType.CONNECTION_READY);
                 var dealerMon = dealer.monitorOpen(MonitorEventType.CONNECTION_READY)) {
                dealer.setRoutingId(RoutingId.copyOf(
                    "request-reply-client".getBytes(StandardCharsets.UTF_8)));
                router.bind(endpoint);
                dealer.connect(endpoint);
                routerMon.recv();
                dealerMon.recv();
            }

            // Install callback AFTER connection readiness confirmed.
            requestRouter.onReceive(received -> {
                try {
                    RoutingId rid = received.routingId();
                    byte[] data = received.singlePartOrThrow().toByteArray();
                    String payload = new String(data, StandardCharsets.UTF_8);
                    assertTrue(payload.startsWith("request-"));
                    String index = payload.substring("request-".length());
                    try (Message reply =
                             Message.copyOfUtf8("reply-" + index)) {
                        requestRouter.reply(rid, received.requestSequence(),
                            reply, SendFlags.DONT_WAIT);
                    }
                } catch (Throwable t) {
                    if (callbackError.get() == null)
                        callbackError.set(t);
                }
            });

            for (int i = 0; i < roundCount; i++) {
                int index = i;
                requestDealer.request(Message.copyOfUtf8("request-" + i),
                    (result, received) -> {
                        try {
                            assertEquals(RequestResult.OK, result);
                            byte[] data = received.singlePartOrThrow().toByteArray();
                            String payload = new String(data, StandardCharsets.UTF_8);
                            assertTrue(payload.startsWith("reply-"));
                            assertEquals("reply-" + index,
                                payload);
                        } catch (Throwable t) {
                            if (callbackError.get() == null)
                                callbackError.set(t);
                        } finally {
                            allReplies.countDown();
                        }
                    }, SendFlags.NONE,
                    Duration.ofMillis(TestSupport.DEFAULT_TIMEOUT_MS));
            }

            assertTrue(allReplies.await(
                    TestSupport.DEFAULT_TIMEOUT_MS, TimeUnit.MILLISECONDS),
                "multi-round callback+send timed out");
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
