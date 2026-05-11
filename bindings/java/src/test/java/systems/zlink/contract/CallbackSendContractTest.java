/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contract;

import systems.zlink.Context;
import systems.zlink.DealerSocket;
import systems.zlink.Message;
import systems.zlink.MonitorEventType;
import systems.zlink.PairSocket;
import systems.zlink.RequestResult;
import systems.zlink.RouterSocket;
import systems.zlink.RoutingId;
import systems.zlink.SendFlags;
import systems.zlink.StreamSocket;
import systems.zlink.TestSupport;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.Arrays;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
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

            Thread routerThread = new Thread(() -> {
                try (systems.zlink.Received received = new systems.zlink.Received()) {

                    router.recv(received, systems.zlink.RecvFlags.NONE);
                    RoutingId rid = received.routingId().orElseThrow();
                    assertNotNull(rid,
                        "router must receive routing id from dealer");
                    try (Message reply = Message.copyOfUtf8("pong")) {
                        received.reply(reply);
                    }
                } catch (Throwable t) {
                    callbackError.set(t);
                }
            }, "router-send-inside-recv");
            routerThread.start();

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

            Thread rightThread = new Thread(() -> {
                try (systems.zlink.Received received = new systems.zlink.Received()) {

                    right.recv(received, systems.zlink.RecvFlags.NONE);
                    byte[] data = received.singlePartOrThrow().toByteArray();
                    assertEquals("ping",
                        new String(data, StandardCharsets.UTF_8));
                    try (Message reply = Message.copyOfUtf8("pong")) {
                        right.send(reply, SendFlags.DONT_WAIT);
                    }
                } catch (Throwable t) {
                    callbackError.set(t);
                }
            }, "pair-send-inside-recv-right");
            rightThread.start();

            Thread leftThread = new Thread(() -> {
                try (systems.zlink.Received received = new systems.zlink.Received()) {

                    left.recv(received, systems.zlink.RecvFlags.NONE);
                    replyPayload.set(received.singlePartOrThrow().toByteArray());
                } catch (Throwable t) {
                    callbackError.set(t);
                } finally {
                    replyReceived.countDown();
                }
            }, "pair-send-inside-recv-left");
            leftThread.start();

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

        try (Context ctx = new Context()) {
            for (int i = 0; i < roundCount; i++) {
                int index = i;
                CountDownLatch roundDone = new CountDownLatch(1);
                CountDownLatch routerDone = new CountDownLatch(1);
                try (RouterSocket router = new RouterSocket(ctx);
                     DealerSocket dealer = new DealerSocket(ctx)) {
                    String endpoint = TestSupport.tcpEndpoint();
                    try (var routerMon = router.monitorOpen(
                             MonitorEventType.CONNECTION_READY);
                         var dealerMon = dealer.monitorOpen(
                             MonitorEventType.CONNECTION_READY)) {
                        dealer.setRoutingId(RoutingId.fromBytes(
                            "request-reply-client"
                                .getBytes(StandardCharsets.UTF_8)));
                        router.bind(endpoint);
                        dealer.connect(endpoint);
                        routerMon.recv();
                        dealerMon.recv();
                    }
                    TestSupport.allowTcpRequestReplyCallbackHandshakeToSettle();

                    Thread routerThread = new Thread(() -> {
                        try (systems.zlink.Received received = new systems.zlink.Received()) {

                            router.recv(received, systems.zlink.RecvFlags.NONE);
                            RoutingId rid = received.routingId().orElseThrow();
                            byte[] data = received.singlePartOrThrow()
                                .toByteArray();
                            String payload = new String(data,
                                StandardCharsets.UTF_8);
                            assertTrue(payload.startsWith("request-"));
                            String requestIndex = payload.substring(
                                "request-".length());
                            try (Message reply = Message.copyOfUtf8(
                                     "reply-" + requestIndex)) {
                                received.reply(reply);
                            }
                        } catch (Throwable t) {
                            if (callbackError.get() == null)
                                callbackError.set(t);
                        } finally {
                            routerDone.countDown();
                        }
                    }, "router-multi-round-recv-" + index);
                    routerThread.start();

                    try (Message request = Message.copyOfUtf8("request-" + index)) {
                        dealer.request(request,
                            (result, received) -> {
                                try {
                                    assertEquals(RequestResult.OK, result);
                                    byte[] data = received.get(0)
                                        .toByteArray();
                                    String payload = new String(data,
                                        StandardCharsets.UTF_8);
                                    assertTrue(payload.startsWith("reply-"));
                                    assertEquals("reply-" + index, payload);
                                } catch (Throwable t) {
                                    if (callbackError.get() == null)
                                        callbackError.set(t);
                                } finally {
                                    Message.closeAll(received);
                                    roundDone.countDown();
                                    allReplies.countDown();
                                }
                            }, SendFlags.NONE,
                            Duration.ofMillis(TestSupport.DEFAULT_TIMEOUT_MS));
                    }

                    assertTrue(roundDone.await(
                            TestSupport.DEFAULT_TIMEOUT_MS,
                            TimeUnit.MILLISECONDS),
                        "round callback timed out");
                    assertTrue(routerDone.await(
                            TestSupport.DEFAULT_TIMEOUT_MS,
                            TimeUnit.MILLISECONDS),
                        "router recv timed out");
                }
            }
        }

        assertTrue(allReplies.await(
                TestSupport.DEFAULT_TIMEOUT_MS, TimeUnit.MILLISECONDS),
            "multi-round callback+send timed out");
        assertNull(callbackError.get(),
            "callback raised: " + callbackError.get());
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
            server.onPacket((routingId, header, payload) -> {
                try {
                    if (routingId == null)
                        return;
                    int size = payload.size();
                    if (size == 0)
                        return;
                    try (Message reply = frame(Message.copyOf(new byte[0]),
                            payload)) {
                        server.send(routingId, reply, SendFlags.DONT_WAIT);
                    }
                    echoed.countDown();
                } catch (Throwable t) {
                    callbackError.compareAndSet(null, t);
                }
            });

            try (java.net.Socket client = new java.net.Socket("127.0.0.1", port)) {
                client.setSoTimeout(TestSupport.DEFAULT_TIMEOUT_MS);
                byte[] frame = frameBytes(outbound);
                client.getOutputStream().write(frame);
                client.getOutputStream().flush();
                byte[] echoedPayload = client.getInputStream().readNBytes(frame.length);
                assertArrayEquals(outbound,
                    Arrays.copyOfRange(echoedPayload, 6, echoedPayload.length));
            }

            assertTrue(echoed.await(TestSupport.DEFAULT_TIMEOUT_MS,
                    TimeUnit.MILLISECONDS),
                "stream packet callback echo timed out");
            assertNull(callbackError.get(),
                "callback raised: " + callbackError.get());
        }
    }

    @Test
    public void blockingSendInsideOnReceiveIsRejectedExplicitly() throws Exception {
        TestSupport.assumeNative();
        assertFalse(Arrays.stream(PairSocket.class.getMethods())
            .anyMatch(method -> method.getName().equals("onReceive")));
    }

    private static byte[] frameBytes(byte[] body) {
        byte[] frame = new byte[6 + body.length];
        frame[2] = (byte) (body.length >>> 24);
        frame[3] = (byte) (body.length >>> 16);
        frame[4] = (byte) (body.length >>> 8);
        frame[5] = (byte) body.length;
        System.arraycopy(body, 0, frame, 6, body.length);
        return frame;
    }

    private static Message frame(Message header, Message body) {
        try (header) {
            byte[] headerBytes = header.toByteArray();
            byte[] bodyBytes = body.toByteArray();
            byte[] frame = new byte[6 + headerBytes.length + bodyBytes.length];
            frame[0] = (byte) (headerBytes.length >>> 8);
            frame[1] = (byte) headerBytes.length;
            frame[2] = (byte) (bodyBytes.length >>> 24);
            frame[3] = (byte) (bodyBytes.length >>> 16);
            frame[4] = (byte) (bodyBytes.length >>> 8);
            frame[5] = (byte) bodyBytes.length;
            System.arraycopy(headerBytes, 0, frame, 6, headerBytes.length);
            System.arraycopy(bodyBytes, 0, frame, 6 + headerBytes.length,
                bodyBytes.length);
            return Message.copyOf(frame);
        }
    }
}
