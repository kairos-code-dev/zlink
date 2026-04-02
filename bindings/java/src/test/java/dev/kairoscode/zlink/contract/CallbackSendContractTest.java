/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.TestSupport;
import java.nio.charset.StandardCharsets;
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
 * <p>The core C API ({@code zlink_send_rid} / {@code zlink_send}) is
 * thread-safe and supports being called from within a
 * {@code zlink_recv_handler} callback.  Before the fix in Socket/SocketCore,
 * blocking sends from callbacks could deadlock the socket I/O thread
 * because the retry loop waited for I/O events that could only be
 * delivered by the very same thread.
 *
 * <p>The fix forces {@code DONTWAIT} on sends issued from callback
 * context so the I/O thread is never blocked in a retry loop.
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
            int connReady = MonitorEventType.CONNECTION_READY.getValue();
            try (var routerMon = router.monitorOpen(connReady);
                 var dealerMon = dealer.monitorOpen(connReady)) {
                router.bind(endpoint);
                dealer.connect(endpoint);
                routerMon.recv();
                dealerMon.recv();
            }

            // Install callback AFTER bind+connect+waitConnected so the
            // peer routing-id handshake completes through the normal
            // code path (xattach_pipe without dispatch mode active).
            router.onReceive(received -> {
                try {
                    RoutingId rid = received.routingId();
                    assertNotNull(rid,
                        "router must receive routing id from dealer");
                    try (Message reply = Message.copyOfUtf8("pong")) {
                        router.send(rid, reply);
                    }
                } catch (Throwable t) {
                    callbackError.set(t);
                }
            });

            try (Message request = Message.copyOfUtf8("ping")) {
                dealer.send(request);
            }

            // Dealer receives the reply via callback.
            dealer.onReceive(received -> {
                try {
                    replyPayload.set(
                        received.singlePartOrThrow().toByteArray());
                } catch (Throwable t) {
                    callbackError.set(t);
                } finally {
                    replyReceived.countDown();
                }
            });

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
            int connReady = MonitorEventType.CONNECTION_READY.getValue();
            try (var leftMon = left.monitorOpen(connReady);
                 var rightMon = right.monitorOpen(connReady)) {
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
                        right.send(reply);
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
            int connReady = MonitorEventType.CONNECTION_READY.getValue();
            try (var routerMon = router.monitorOpen(connReady);
                 var dealerMon = dealer.monitorOpen(connReady)) {
                router.bind(endpoint);
                dealer.connect(endpoint);
                routerMon.recv();
                dealerMon.recv();
            }

            // Install callback AFTER connection readiness confirmed.
            router.onReceive(received -> {
                try {
                    RoutingId rid = received.routingId();
                    byte[] data = received.singlePartOrThrow().toByteArray();
                    String payload = new String(data, StandardCharsets.UTF_8);
                    assertTrue(payload.startsWith("request-"));
                    String index = payload.substring("request-".length());
                    try (Message reply =
                             Message.copyOfUtf8("reply-" + index)) {
                        router.send(rid, reply);
                    }
                } catch (Throwable t) {
                    if (callbackError.get() == null)
                        callbackError.set(t);
                }
            });

            dealer.onReceive(received -> {
                try {
                    byte[] data = received.singlePartOrThrow().toByteArray();
                    String payload = new String(data, StandardCharsets.UTF_8);
                    assertTrue(payload.startsWith("reply-"));
                } catch (Throwable t) {
                    if (callbackError.get() == null)
                        callbackError.set(t);
                } finally {
                    allReplies.countDown();
                }
            });

            for (int i = 0; i < roundCount; i++) {
                try (Message msg =
                         Message.copyOfUtf8("request-" + i)) {
                    dealer.send(msg);
                }
            }

            assertTrue(allReplies.await(
                    TestSupport.DEFAULT_TIMEOUT_MS, TimeUnit.MILLISECONDS),
                "multi-round callback+send timed out");
            assertNull(callbackError.get(),
                "callback raised: " + callbackError.get());
        }
    }
}
