package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.StreamSocket;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.TopicMessage;
import dev.kairoscode.zlink.XPubSocket;
import dev.kairoscode.zlink.XPubSubscriptionMode;
import java.lang.reflect.Method;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class SocketContractTest {
    @Test
    public void sendAndRecvUseCanonicalMultipartSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx)) {
            String endpoint = TestSupport.inprocEndpoint("socket-contract");
            server.bind(endpoint);
            client.connect(endpoint);

            try (Message outbound = Message.copyOfUtf8("pair-contract")) {
                client.send(outbound);
            }

            try (var received = server.recv()) {
                assertArrayEquals("pair-contract".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
            }
        }
    }

    @Test
    public void publishAndSubscribeUseCanonicalTopicAwareSurface() {
        TestSupport.assumeNative();

        int readyEvents = dev.kairoscode.zlink.MonitorEventType.SUB_DELIVERY_READY_CHANGED
            .getValue()
            | dev.kairoscode.zlink.MonitorEventType.PUB_DELIVERY_READY_CHANGED
                .getValue();
        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(readyEvents);
             var subMonitor = sub.monitorOpen(readyEvents)) {
            String endpoint = TestSupport.inprocEndpoint("socket-pubsub-contract");
            pub.bind(endpoint);
            sub.setSubscription("socket-topic");
            sub.connect(endpoint);
            TestSupport.awaitDeliveryReady(subMonitor,
                dev.kairoscode.zlink.MonitorEventType.SUB_DELIVERY_READY_CHANGED);
            TestSupport.awaitDeliveryReady(pubMonitor,
                dev.kairoscode.zlink.MonitorEventType.PUB_DELIVERY_READY_CHANGED);

            try (Message payload = Message.copyOfUtf8("socket-payload")) {
                pub.publish("socket-topic", payload);
            }

            try (TopicMessage received = sub.subscribe()) {
                assertEquals("socket-topic", received.topicId());
                assertArrayEquals("socket-payload".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
            }
        }
    }

    @Test
    public void routerOwnRoutingIdAndTlsSurfaceUseTypedSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket router = new RouterSocket(ctx)) {
            RoutingId routerRid = RoutingId.copyOf("router-self".getBytes(StandardCharsets.UTF_8));
            router.setRoutingId(routerRid);
            assertArrayEquals(routerRid.toByteArray(), router.routingId().toByteArray());

            String cert = Path.of("tests/certs/server.crt").toAbsolutePath().toString();
            String key = Path.of("tests/certs/server.key").toAbsolutePath().toString();
            String ca = Path.of("tests/certs/ca.crt").toAbsolutePath().toString();

            assertDoesNotThrow(() -> router.setTlsServer(cert, key, true));
            assertDoesNotThrow(() -> router.setTlsClient(ca, "localhost", true));
        }
    }

    @Test
    public void pairSocketDoesNotExposeLegacyStreamOrTopicSurface() {
        assertFalse(hasPublicMethod(PairSocket.class, "attachStream"));
        assertFalse(hasPublicMethod(PairSocket.class, "streamSend"));
        assertFalse(hasPublicMethod(PairSocket.class, "subscriptionEvent"));
        assertFalse(hasPublicMethod(PairSocket.class, "publish"));
        assertFalse(hasPublicMethod(PairSocket.class, "setSubscription"));
    }

    @Test
    public void streamSocketDoesNotExposeLegacyStreamOrConnectSurface() {
        assertFalse(hasPublicMethod(StreamSocket.class, "attachStream"));
        assertFalse(hasPublicMethod(StreamSocket.class, "streamSend"));
        assertFalse(hasPublicMethod(StreamSocket.class, "connect"));
        assertFalse(hasPublicMethod(StreamSocket.class, "attachDiscovery"));
    }

    @Test
    public void rawOptionSurfaceIsHiddenAndTypedOptionsRemain() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket pair = new PairSocket(ctx);
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             StreamSocket stream = new StreamSocket(ctx);
             XPubSocket xpub = new XPubSocket(ctx)) {
            assertFalse(hasPublicMethod(PairSocket.class, "setOption"));
            assertFalse(hasPublicMethod(PairSocket.class, "getOption"));
            assertFalse(hasPublicMethod(PairSocket.class, "setSockOpt"));
            assertFalse(hasPublicMethod(PairSocket.class, "getSockOptInt"));
            assertFalse(hasPublicMethod(MonitorSocket.class, "setOption"));
            assertFalse(hasPublicMethod(MonitorSocket.class, "recv",
                dev.kairoscode.zlink.ReceiveFlag.class));
            assertFalse(hasPublicMethod(PairSocket.class, "send", Message.class,
                dev.kairoscode.zlink.SendFlag.class));
            assertFalse(hasPublicMethod(PairSocket.class, "recv",
                dev.kairoscode.zlink.ReceiveFlag.class));
            assertFalse(hasPublicMethod(PubSocket.class, "publish", String.class,
                Message.class, dev.kairoscode.zlink.SendFlag.class));
            assertFalse(hasPublicMethod(SubSocket.class, "subscribe",
                dev.kairoscode.zlink.ReceiveFlag.class));
            assertFalse(hasPublicMethod(XPubSocket.class, "subscriptionEvent"));
            assertTrue(hasPublicMethod(PairSocket.class, "trySend", Message.class));
            assertTrue(hasPublicMethod(PairSocket.class, "tryRecv"));
            assertTrue(hasPublicMethod(MonitorSocket.class, "tryRecv"));
            assertTrue(hasPublicMethod(PubSocket.class, "tryPublish",
                String.class, Message.class));
            assertTrue(hasPublicMethod(SubSocket.class, "trySubscribe"));
            assertTrue(hasPublicMethod(XPubSocket.class,
                "tryReceiveSubscriptionEvent"));
            assertDoesNotThrow(() ->
                xpub.options().subscriptionMode(XPubSubscriptionMode.MANUAL));
            assertDoesNotThrow(() -> stream.options().notifyConnections(true));
            assertTrue(stream.options().notifyConnections());
            assertDoesNotThrow(() -> pair.options().receiveTimeoutMillis(10));
            assertEquals(10, pair.options().receiveTimeoutMillis());
            assertDoesNotThrow(() -> pair.options().sendTimeout(Duration.ofMillis(20)));
            assertEquals(Duration.ofMillis(20), pair.options().sendTimeout());
        }
    }

    @Test
    public void trySendAndTryRecvUseCanonicalNonBlockingSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx)) {
            String endpoint = TestSupport.inprocEndpoint("socket-try-contract");
            server.bind(endpoint);
            client.connect(endpoint);

            assertTrue(server.tryRecv().isEmpty());
            try (Message outbound = Message.copyOfUtf8("pair-try")) {
                assertEquals(SendResult.SENT, client.trySend(outbound));
            }
            try (var received = server.tryRecv().orElseThrow()) {
                assertEquals(List.of("pair-try"),
                    List.of(received.singlePartOrThrow().toUtf8String()));
            }
        }
    }

    private static boolean hasPublicMethod(Class<?> type, String name) {
        for (Method method : type.getMethods()) {
            if (method.getName().equals(name)) {
                return true;
            }
        }
        return false;
    }

    private static boolean hasPublicMethod(Class<?> type, String name,
                                           Class<?>... parameterTypes) {
        try {
            Method method = type.getMethod(name, parameterTypes);
            return method != null;
        } catch (NoSuchMethodException ex) {
            return false;
        }
    }
}
