package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.ErrorCode;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.PubSocketOptions;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.ServiceType;
import dev.kairoscode.zlink.StreamSocket;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.SubSocketOptions;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.TopicMessage;
import dev.kairoscode.zlink.XPubSocket;
import dev.kairoscode.zlink.XSubSocket;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.spot.Spot;
import java.lang.foreign.MemorySegment;
import java.lang.reflect.Constructor;
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
    private static final Class<?> RECEIVE_FLAG_CLASS =
        loadClass("dev.kairoscode.zlink.ReceiveFlag");
    private static final Class<?> SEND_FLAG_CLASS =
        loadClass("dev.kairoscode.zlink.SendFlag");

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

        int readyEvents = dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY
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
            TestSupport.awaitMonitorEvent(subMonitor,
                dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY);
            TestSupport.awaitMonitorEvent(pubMonitor,
                dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY);

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
    public void attachDiscoveryIsOnlyExposedOnSupportedSocketTypes() {
        assertFalse(hasPublicMethod(PairSocket.class, "attachDiscovery"));
        assertTrue(hasPublicMethod(DealerSocket.class, "attachDiscovery",
            Discovery.class));
        assertTrue(hasPublicMethod(RouterSocket.class, "attachDiscovery",
            Discovery.class));
        assertTrue(hasPublicMethod(PubSocket.class, "attachDiscovery",
            Discovery.class));
        assertTrue(hasPublicMethod(SubSocket.class, "attachDiscovery",
            Discovery.class));
        assertFalse(hasPublicMethod(XPubSocket.class, "attachDiscovery"));
        assertFalse(hasPublicMethod(XSubSocket.class, "attachDiscovery"));
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
             XPubSocket xpub = new XPubSocket(ctx);
             XSubSocket xsub = new XSubSocket(ctx)) {
            assertFalse(hasPublicMethod(PairSocket.class, "setOption"));
            assertFalse(hasPublicMethod(PairSocket.class, "getOption"));
            assertFalse(hasPublicMethod(PairSocket.class, "setSockOpt"));
            assertFalse(hasPublicMethod(PairSocket.class, "getSockOptInt"));
            assertFalse(hasPublicMethod(MonitorSocket.class, "setOption"));
            assertFalse(hasPublicMethod(Context.class, "handle"));
            assertFalse(hasPublicMethod(Discovery.class, "handle"));
            assertFalse(hasPublicMethod(Spot.class, "handle"));
            assertFalse(hasPublicMethod(MonitorSocket.class, "recv",
                RECEIVE_FLAG_CLASS));
            assertFalse(hasPublicMethod(PairSocket.class, "send", Message.class,
                SEND_FLAG_CLASS));
            assertFalse(hasPublicMethod(PairSocket.class, "recv",
                RECEIVE_FLAG_CLASS));
            assertFalse(hasPublicMethod(PubSocket.class, "publish", String.class,
                Message.class, SEND_FLAG_CLASS));
            assertFalse(hasPublicMethod(SubSocket.class, "subscribe",
                RECEIVE_FLAG_CLASS));
            assertFalse(hasPublicMethod(Message.class, "dataSegment"));
            assertFalse(hasPublicMethod(Message.class, "dataSegment", int.class));
            assertFalse(hasPublicMethod(Message.class, "copyTo",
                MemorySegment.class));
            assertFalse(hasPublicMethod(Message.class, "moveTo",
                MemorySegment.class));
            assertFalse(hasPublicMethod(Message.class, "fromMsgVector",
                MemorySegment.class, long.class));
            assertFalse(hasPublicMethod(Message.class, "fromOwnedMsgVector",
                MemorySegment.class, long.class));
            assertFalse(hasPublicMethod(XPubSocket.class, "subscriptionEvent"));
            assertFalse(hasPublicConstructor(ServiceMonitor.class,
                MemorySegment.class));
            assertTrue(hasPublicMethod(PairSocket.class, "trySend", Message.class));
            assertTrue(hasPublicMethod(PairSocket.class, "tryRecv"));
            assertTrue(hasPublicMethod(MonitorSocket.class, "tryRecv"));
            assertTrue(hasPublicMethod(PubSocket.class, "tryPublish",
                String.class, Message.class));
            assertTrue(hasPublicMethod(SubSocket.class, "trySubscribe"));
            assertTrue(hasPublicMethod(XPubSocket.class,
                "tryReceiveSubscriptionEvent"));
            assertEquals(PubSocketOptions.class, pub.options().getClass());
            assertTrue(hasPublicMethod(PubSocketOptions.class, "verbose"));
            assertTrue(hasPublicMethod(PubSocketOptions.class, "verboser"));
            assertTrue(hasPublicMethod(PubSocketOptions.class, "noDrop"));
            assertTrue(hasPublicMethod(PubSocketOptions.class, "manual"));
            assertDoesNotThrow(() -> xpub.options().manual(true));
            assertDoesNotThrow(() -> xpub.options().verbose(true));
            assertDoesNotThrow(() -> xpub.options().verboser(true));
            assertDoesNotThrow(() -> xpub.options().noDrop(true));
            assertDoesNotThrow(() -> xpub.options().welcomeMessage("hello"));
            assertEquals(SubSocketOptions.class, sub.options().getClass());
            assertTrue(hasPublicMethod(SubSocketOptions.class, "topicsCount"));
            assertEquals(0, sub.options().topicsCount());
            assertEquals(SubSocketOptions.class, xsub.options().getClass());
            assertEquals(0, xsub.options().topicsCount());
            assertDoesNotThrow(() -> stream.options().notify(true));
            assertTrue(stream.options().notifyEnabled());
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

    @Test
    public void attachDiscoveryGatesManualPeerApisAndSocketClose() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Discovery discovery = new Discovery(ctx, ServiceType.SOCKET,
               "socket-svc")) {
            DealerSocket dealer = new DealerSocket(ctx);
            dealer.attachDiscovery(discovery);

            ZlinkException connectError = assertThrows(ZlinkException.class,
                () -> dealer.connect("tcp://127.0.0.1:39001"));
            assertEquals(ErrorCode.EFSM, connectError.errorCode());

            ZlinkException disconnectError = assertThrows(ZlinkException.class,
                () -> dealer.disconnect("tcp://127.0.0.1:39001"));
            assertEquals(ErrorCode.EFSM, disconnectError.errorCode());

            ZlinkException unbindError = assertThrows(ZlinkException.class,
                () -> dealer.unbind("tcp://127.0.0.1:39001"));
            assertEquals(ErrorCode.EFSM, unbindError.errorCode());

            ZlinkException closeError = assertThrows(ZlinkException.class,
                dealer::close);
            assertEquals(ErrorCode.EFSM, closeError.errorCode());
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

    private static boolean hasPublicConstructor(Class<?> type,
                                                Class<?>... parameterTypes) {
        try {
            Constructor<?> ctor = type.getConstructor(parameterTypes);
            return ctor != null;
        } catch (NoSuchMethodException ex) {
            return false;
        }
    }

    private static Class<?> loadClass(String name) {
        try {
            return Class.forName(name);
        } catch (ClassNotFoundException ex) {
            throw new IllegalStateException("missing contract class " + name,
                ex);
        }
    }
}
