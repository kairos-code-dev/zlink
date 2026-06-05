package systems.zlink.contract;

import systems.zlink.TestSupport;
import systems.zlink.contracts.service.spot.ActorBindOperation;
import systems.zlink.contracts.service.spot.ActorJoinCallbackSubmitOperation;
import systems.zlink.contracts.service.spot.ActorJoinEntrySpotOperation;
import systems.zlink.contracts.service.spot.ActorJoinOperation;
import systems.zlink.contracts.service.spot.ActorJoinSubmitOperation;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ActorRoute;
import systems.zlink.contracts.service.spot.ActorUnbindOperation;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.sockets.CommonSocketOptions;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.service.registry.MemberPeerEntry;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.eventing.MonitorStatus;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.contracts.sockets.PairSocket;
import systems.zlink.contracts.eventing.PollEventFlags;
import systems.zlink.contracts.eventing.PollEvents;
import systems.zlink.contracts.eventing.Poller;
import systems.zlink.contracts.sockets.PubSocket;
import systems.zlink.contracts.sockets.PubSocketOptions;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.service.registry.Registry;
import systems.zlink.contracts.service.spot.ReplyOperation;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.service.spot.RequestOperation;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.internal.sockets.SendFlag;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.contracts.sockets.SocketType;
import systems.zlink.contracts.sockets.SubmitRetryMode;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotDispatchEventHandler;
import systems.zlink.contracts.service.spot.SpotDispatchInfo;
import systems.zlink.contracts.service.spot.SpotKind;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.service.spot.SpotNodeActorEntry;
import systems.zlink.contracts.service.spot.SpotNodeSocketFilter;
import systems.zlink.contracts.sockets.StreamPacketHandler;
import systems.zlink.contracts.sockets.StreamSocket;
import systems.zlink.contracts.sockets.SubSocket;
import systems.zlink.contracts.sockets.SubSocketOptions;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.contracts.messaging.SubscriptionEntry;
import systems.zlink.contracts.eventing.ZlinkTimer;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.sockets.XPubSocket;
import systems.zlink.contracts.sockets.XSubSocket;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.contracts.core.ZlinkVersion;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

public class SocketContractTest {
    private static final int ERRNO_EFSM = 156384763;
    private static final Class<?> MEMORY_SEGMENT_CLASS =
        classNamed("java.lang.foreign.MemorySegment");

    @Test
    public void sendAndRecvUseCanonicalMultipartSurface() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             PairSocket server = ctx.createPairSocket();
             PairSocket client = ctx.createPairSocket()) {
            String endpoint = TestSupport.inprocEndpoint("socket-contract");
            server.bind(endpoint);
            client.connect(endpoint);

            try (Message outbound = Message.from("pair-contract")) {
                client.send().message(outbound).submit();
            }

            try (systems.zlink.contracts.messaging.Received received = new systems.zlink.contracts.messaging.Received()) {


                server.recv(received, systems.zlink.contracts.sockets.RecvFlags.NONE);
                assertArrayEquals("pair-contract".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
            }
        }
    }

    @Test
    public void requestReplyWrapperSupportsDealerRouterRoundTrip() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             RouterSocket routerSocket = ctx.createRouterSocket();
             DealerSocket dealerSocket = ctx.createDealerSocket();
             ExecutorService serverExecutor = daemonExecutor("zlink-socket-contract")) {
            String endpoint = TestSupport.inprocEndpoint("request-reply");
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);

            CompletableFuture<Void> server = CompletableFuture.runAsync(() -> {
                try (systems.zlink.contracts.messaging.Received received = new systems.zlink.contracts.messaging.Received()) {

                    routerSocket.recv(received, systems.zlink.contracts.sockets.RecvFlags.NONE);
                    assertArrayEquals("ping".getBytes(StandardCharsets.UTF_8),
                        received.singlePartOrThrow().toByteArray());
                    assertTrue(received.getRoutingId().isPresent());
                    assertTrue(received.requestSeq().isPresent());
                    assertTrue(received.requestSeq().orElseThrow() != 0L);
                    received.reply()
                        .message(Message.from("pong"))
                        .submit();
                }
            }, serverExecutor);

            try (Message request = Message.from("ping")) {
                List<Message> reply = dealerSocket.request()
                    .message(request)
                    .timeout(Duration.ofSeconds(2))
                    .submitAsync()
                    .toCompletableFuture()
                    .get(2, TimeUnit.SECONDS);
                try {
                    assertArrayEquals("pong".getBytes(StandardCharsets.UTF_8),
                        reply.get(0).toByteArray());
                } finally {
                    Message.closeAll(reply);
                }
            }
            server.get(2, TimeUnit.SECONDS);
        }
    }

    @Test
    public void requestReplyWrapperSupportsMultipartDealerRequest() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             RouterSocket routerSocket = ctx.createRouterSocket();
             DealerSocket dealerSocket = ctx.createDealerSocket();
             ExecutorService serverExecutor =
                 daemonExecutor("zlink-socket-contract-multipart-request")) {
            String endpoint = TestSupport.inprocEndpoint(
                "request-reply-multipart");
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);

            CompletableFuture<Void> server = CompletableFuture.runAsync(() -> {
                try (systems.zlink.contracts.messaging.Received received =
                         new systems.zlink.contracts.messaging.Received()) {

                    routerSocket.recv(received,
                        systems.zlink.contracts.sockets.RecvFlags.NONE);
                    assertEquals(2, received.parts().size());
                    assertArrayEquals("Packet".getBytes(StandardCharsets.UTF_8),
                        received.parts().get(0).toByteArray());
                    assertArrayEquals("payload".getBytes(StandardCharsets.UTF_8),
                        received.parts().get(1).toByteArray());
                    received.reply()
                        .message(Message.from("ok"))
                        .submit();
                }
            }, serverExecutor);

            try (Message packet = Message.from("Packet");
                 Message payload = Message.from("payload")) {
                List<Message> reply = dealerSocket.request()
                    .message(packet)
                    .message(payload)
                    .timeout(Duration.ofSeconds(2))
                    .submitAsync()
                    .toCompletableFuture()
                    .get(2, TimeUnit.SECONDS);
                try {
                    assertArrayEquals("ok".getBytes(StandardCharsets.UTF_8),
                        reply.get(0).toByteArray());
                } finally {
                    Message.closeAll(reply);
                }
            }
            server.get(2, TimeUnit.SECONDS);
        }
    }

    @Test
    public void requestReplyCallbackSupportsMultipartDealerRequest() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             RouterSocket routerSocket = ctx.createRouterSocket();
             DealerSocket dealerSocket = ctx.createDealerSocket();
             ExecutorService serverExecutor =
                 daemonExecutor("zlink-socket-contract-multipart-callback")) {
            String endpoint = TestSupport.inprocEndpoint(
                "request-reply-multipart-callback");
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);

            CompletableFuture<Void> server = CompletableFuture.runAsync(() -> {
                try (systems.zlink.contracts.messaging.Received received =
                         new systems.zlink.contracts.messaging.Received()) {

                    routerSocket.recv(received,
                        systems.zlink.contracts.sockets.RecvFlags.NONE);
                    assertEquals(2, received.parts().size());
                    assertArrayEquals("Packet".getBytes(StandardCharsets.UTF_8),
                        received.parts().get(0).toByteArray());
                    assertArrayEquals("payload".getBytes(StandardCharsets.UTF_8),
                        received.parts().get(1).toByteArray());
                    received.reply()
                        .message(Message.from("ok"))
                        .submit();
                }
            }, serverExecutor);

            CountDownLatch done = new CountDownLatch(1);
            AtomicReference<RequestResult> resultRef = new AtomicReference<>();
            AtomicReference<List<Message>> replyRef = new AtomicReference<>();
            try (Message packet = Message.from("Packet");
                 Message payload = Message.from("payload")) {
                dealerSocket.request()
                    .message(packet)
                    .message(payload)
                    .timeout(Duration.ofSeconds(2))
                    .submit((result, reply) -> {
                        resultRef.set(result);
                        replyRef.set(reply);
                        done.countDown();
                    });
                assertTrue(done.await(2, TimeUnit.SECONDS));
                assertEquals(RequestResult.OK, resultRef.get());
                List<Message> reply = replyRef.get();
                try {
                    assertArrayEquals("ok".getBytes(StandardCharsets.UTF_8),
                        reply.get(0).toByteArray());
                } finally {
                    Message.closeAll(reply);
                }
            }
            server.get(2, TimeUnit.SECONDS);
        }
    }

    @Test
    public void requestReplyWrapperPreservesDataReceiveSurface() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             RouterSocket routerSocket = ctx.createRouterSocket();
             DealerSocket dealerSocket = ctx.createDealerSocket()) {
            String endpoint = TestSupport.inprocEndpoint("request-reply-data");
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);

            dealerSocket.send()
                .message(Message.from("plain-data"))
                .submit();

            try (systems.zlink.contracts.messaging.Received received = new systems.zlink.contracts.messaging.Received()) {


                routerSocket.recv(received, systems.zlink.contracts.sockets.RecvFlags.NONE);
                assertArrayEquals("plain-data".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
            }
        }
    }

    @Test
    public void requestReplyCallbackCompletesBeforeSocketClose() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             RouterSocket routerSocket = ctx.createRouterSocket();
             DealerSocket dealerSocket = ctx.createDealerSocket();
             ExecutorService serverExecutor =
                 daemonExecutor("zlink-socket-contract-callback")) {
            String endpoint = TestSupport.inprocEndpoint("request-reply-callback");
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);

            CompletableFuture<Void> server = CompletableFuture.runAsync(() -> {
                try (systems.zlink.contracts.messaging.Received received = new systems.zlink.contracts.messaging.Received()) {

                    routerSocket.recv(received, systems.zlink.contracts.sockets.RecvFlags.NONE);
                    received.reply()
                        .message(Message.from("pong-callback"))
                        .submit();
                }
            }, serverExecutor);

            CountDownLatch done = new CountDownLatch(1);
            AtomicReference<List<Message>> replyRef = new AtomicReference<>();
            AtomicReference<RequestResult> resultRef = new AtomicReference<>();
            AtomicReference<Throwable> errorRef = new AtomicReference<>();
            try (Message request = Message.from("ping-callback")) {
                dealerSocket.request().message(request).submit((result, reply) -> {
                    resultRef.set(result);
                    replyRef.set(reply);
                    done.countDown();
                });
                assertTrue(done.await(2, TimeUnit.SECONDS));
                assertEquals(RequestResult.OK, resultRef.get());
                List<Message> reply = replyRef.get();
                assertNotNull(reply);
                try {
                    assertArrayEquals("pong-callback".getBytes(StandardCharsets.UTF_8),
                        reply.get(0).toByteArray());
                } catch (Throwable error) {
                    errorRef.set(error);
                } finally {
                    Message.closeAll(reply);
                }
            }
            server.get(2, TimeUnit.SECONDS);
            if (errorRef.get() != null) {
                throw new AssertionError(errorRef.get());
            }
        }
    }

    @Test
    public void publishAndSubscribeUseCanonicalTopicAwareSurface() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             PubSocket pub = ctx.createPubSocket();
             SubSocket sub = ctx.createSubSocket();
             var pubMonitor = pub.monitorOpen(
               systems.zlink.contracts.eventing.MonitorEventType.CONNECTION_READY);
             var subMonitor = sub.monitorOpen(
               systems.zlink.contracts.eventing.MonitorEventType.CONNECTION_READY)) {
            String endpoint = TestSupport.inprocEndpoint("socket-pubsub-contract");
            pub.bind(endpoint);
            sub.setSubscription("socket-topic");
            sub.connect(endpoint);
            TestSupport.awaitMonitorEvent(subMonitor,
                systems.zlink.contracts.eventing.MonitorEventType.CONNECTION_READY);
            TestSupport.awaitMonitorEvent(pubMonitor,
                systems.zlink.contracts.eventing.MonitorEventType.CONNECTION_READY);

            try (Message payload = Message.from("socket-payload")) {
                pub.publish("socket-topic").message(payload).submit();
            }

            try (TopicMessage received = new TopicMessage()) {
                assertTrue(sub.subscribe(received, RecvFlags.NONE));
                assertEquals("socket-topic", received.topic());
                assertArrayEquals("socket-payload".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
            }
        }
    }

    @Test
    public void routerOwnRoutingIdAndTlsSurfaceUseTypedSurface() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             RouterSocket router = ctx.createRouterSocket()) {
            RoutingId routerRid = RoutingId.from("router-self".getBytes(StandardCharsets.UTF_8));
            router.setRoutingId(routerRid);
            assertArrayEquals(routerRid.toBytes(), router.getRoutingId().toBytes());

            String cert = Path.of("tests/certs/server.crt").toAbsolutePath().toString();
            String key = Path.of("tests/certs/server.key").toAbsolutePath().toString();
            String ca = Path.of("tests/certs/ca.crt").toAbsolutePath().toString();

            assertDoesNotThrow(() -> router.setTlsServer(cert, key, true));
            assertDoesNotThrow(() -> router.setTlsClient(ca, "localhost", true));
        }
    }

    @Test
    public void serviceLayerTlsAndMonitorSurfaceAreCanonical() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             Registry registry = ctx.createRegistry();
             Discovery discovery = ctx.createDiscovery(AutoConnectType.CLIENT_SERVER, "svc-tls");
             PairSocket socket = ctx.createPairSocket()) {
            assertTrue(hasPublicMethod(Registry.class, "setTlsServer",
                String.class, String.class, boolean.class));
            assertTrue(hasPublicMethod(Registry.class, "setTlsClient",
                String.class, String.class, boolean.class));
            assertFalse(hasPublicMethod(Discovery.class, "setTlsServer",
                String.class, String.class, boolean.class));
            assertTrue(hasPublicMethod(Discovery.class, "setTlsClient",
                String.class, String.class, boolean.class));
            assertTrue(hasPublicMethod(PairSocket.class, "monitorOpen"));
            assertTrue(hasPublicMethod(PairSocket.class, "monitorOpen",
                systems.zlink.contracts.eventing.MonitorEventType[].class));
            assertFalse(hasPublicMethod(PairSocket.class, "monitorOpen",
                int.class));
            assertFalse(hasPublicMethod(Discovery.class, "monitorOpen"));

            String cert = Path.of("tests/certs/server.crt").toAbsolutePath().toString();
            String key = Path.of("tests/certs/server.key").toAbsolutePath().toString();
            String ca = Path.of("tests/certs/ca.crt").toAbsolutePath().toString();
            assertDoesNotThrow(() -> registry.setTlsServer(cert, key, true));
            assertDoesNotThrow(() -> registry.setTlsClient(ca, "localhost", true));
            assertDoesNotThrow(() -> discovery.setTlsClient(ca, "localhost", true));
            assertDoesNotThrow(() -> {
                try (var monitor = socket.monitorOpen()) {
                    assertTrue(monitor.status().sndPendingMsgs() >= 0L);
                }
            });
        }
    }

    @Test
    public void admissionSurfaceMatchesJavaSpec() {
        assertFalse(hasPublicMethod(PairSocket.class, "getAdmissionState"));
        assertFalse(hasPublicMethod(PairSocket.class, "setAdmissionState",
            int.class));
        assertFalse(hasPublicMethod(Spot.class, "getAdmissionState"));
        assertFalse(hasPublicMethod(Spot.class, "setAdmissionState",
            int.class));
    }

    @Test
    public void routedAndLegacySurfaceMatchesJavaSpec() throws Exception {
        assertTrue(hasPublicMethod(RouterSocket.class, "sendToSpot",
            RoutingId.class, RoutingId.class));
        assertTrue(hasPublicMethod(RouterSocket.class, "requestToSpot",
            RoutingId.class, RoutingId.class));
        assertTrue(hasPublicMethod(RouterSocket.class, "replyToSpot",
            RoutingId.class, RoutingId.class, long.class));
        assertFalse(hasPublicMethod(RouterSocket.class, "recvSpot"));
        assertFalse(hasPublicMethod(RouterSocket.class, "recvSpot",
            RecvFlags.class));
        assertFalse(hasPublicMethod(RouterSocket.class, "onSpotReceive"));

        assertTrue(hasPublicMethod(Spot.class, "sendToSpot",
            RoutingId.class, RoutingId.class));
        assertTrue(hasPublicMethod(Spot.class, "sendToSpot",
            RoutingId.class, RoutingId.class)
            && Spot.class.getMethod("sendToSpot", RoutingId.class,
                RoutingId.class).getReturnType() == SendOperation.class);
        assertTrue(hasPublicMethod(Spot.class, "requestToSpot",
            RoutingId.class, RoutingId.class)
            && Spot.class.getMethod("requestToSpot", RoutingId.class,
                RoutingId.class).getReturnType() == RequestOperation.class);
        assertTrue(hasPublicMethod(Spot.class, "replyToSpot",
            RoutingId.class, RoutingId.class, long.class)
            && Spot.class.getMethod("replyToSpot", RoutingId.class,
                RoutingId.class, long.class).getReturnType() == ReplyOperation.class);
        assertTrue(hasPublicMethod(Spot.class, "replyToRouter",
            RoutingId.class, long.class)
            && Spot.class.getMethod("replyToRouter", RoutingId.class,
                long.class).getReturnType() == ReplyOperation.class);
        assertFalse(hasPublicMethod(Spot.class, "sendToSpot",
            RoutingId.class, RoutingId.class, Message.class));
        assertFalse(hasPublicMethod(Spot.class, "replyToSpot",
            RoutingId.class, RoutingId.class, long.class, Message.class));
        assertFalse(hasPublicMethod(Spot.class, "replyToRouter",
            RoutingId.class, long.class, Message.class));
        assertTrue(hasPublicMethod(Spot.class, "recvRouted"));
        assertFalse(hasPublicMethod(Spot.class, "onRoutedReceive"));
        assertTrue(hasPublicMethod(Spot.class, "recvActorLifecycle",
            systems.zlink.contracts.sockets.RecvFlags.class));
        assertTrue(hasPublicMethod(Spot.class, "setDispatchHandler",
            systems.zlink.contracts.service.spot.SpotDispatchEventHandler.class));
        assertFalse(hasPublicMethod(Spot.class, "drainChannelReply",
            systems.zlink.contracts.service.spot.SpotDispatchInfo.class));
        assertFalse(hasPublicMethod(Spot.class, "drainChannelReplyFrom",
            MEMORY_SEGMENT_CLASS));
        assertTrue(hasPublicMethod(Spot.class, "setRoutingId",
            RoutingId.class));
        assertTrue(hasPublicMethod(Spot.class, "getRoutingId"));
        assertTrue(hasPublicMethod(DealerSocket.class, "setChannelName",
            String.class));
        assertTrue(hasPublicMethod(DealerSocket.class, "getChannelName"));
        assertTrue(hasPublicMethod(SpotNode.class, "setRoutingId",
            RoutingId.class));
        assertTrue(hasPublicMethod(SpotNode.class, "setPubBind",
            String.class));
        assertTrue(hasPublicMethod(SpotNode.class, "setRouterBind",
            String.class));
        assertTrue(hasPublicMethod(SpotNode.class, "getRoutingId"));
        assertTrue(hasPublicMethod(SpotNode.class, "entrySpot"));
        assertTrue(hasPublicMethod(SpotNode.class, "spotLookup",
            RoutingId.class));
        assertTrue(hasPublicMethod(SpotNode.class, "getOrCreateSpot",
            RoutingId.class));
        assertTrue(hasPublicMethod(SpotNode.class, "connectRouterChannelPeer",
            String.class, String.class));
        assertTrue(hasPublicMethod(SpotNode.class, "disconnectRouterChannelPeer",
            String.class, String.class));
        assertTrue(hasPublicMethod(SpotNode.class, "disconnectRouterChannelPeerRid",
            String.class, RoutingId.class));
        assertTrue(hasPublicMethod(SpotNode.class,
            "attachSpotRouteChannelDiscovery", String.class, Discovery.class));
        assertFalse(hasPublicMethod(SpotNode.class, "socketSnapshots"));
        assertFalse(hasPublicMethod(SpotNode.class, "socketSnapshots",
            systems.zlink.contracts.service.spot.SpotNodeSocketFilter.class));
        assertTrue(hasPublicMethod(SpotNode.class, "internalSockets"));
        assertTrue(hasPublicMethod(SpotNode.class, "internalSockets",
            systems.zlink.contracts.service.spot.SpotNodeSocketFilter.class));
        assertTrue(hasPublicMethod(SpotNode.class, "routerHwmProfile"));
        assertTrue(hasPublicMethod(SpotNode.class, "routerHighWaterMark", int.class));
        assertTrue(hasPublicMethod(SpotNode.class, "pubSubHwmProfile"));
        assertTrue(hasPublicMethod(SpotNode.class, "pubSubHighWaterMark", int.class));
        assertTrue(hasPublicMethod(Discovery.class, "resolveSpot",
            RoutingId.class));
        assertTrue(hasPublicMethod(Discovery.class, "routeValueMaxSize"));
        assertTrue(hasPublicMethod(Discovery.class, "bindRoute",
            int.class, byte[].class, byte[].class));
        assertTrue(hasPublicMethod(Discovery.class, "unbindRoute",
            int.class, byte[].class));
        assertTrue(hasPublicMethod(Discovery.class, "resolveRoute",
            int.class, byte[].class));
        assertTrue(hasPublicMethod(Discovery.class, "setActorRouteSyncEnabled",
            boolean.class));
        assertTrue(hasPublicMethod(Discovery.class, "isActorRouteSyncEnabled"));
        assertFalse(hasPublicMethod(Discovery.class, "setDealerPeerMode"));

        assertFalse(hasPublicMethod(XPubSocket.class, "onSubscribe"));
        assertFalse(hasPublicMethod(SubSocket.class, "onSubscribe"));
        assertFalse(hasPublicMethod(XSubSocket.class, "onSubscribe"));
        assertFalse(hasPublicMethod(Zlink.class, "errno"));
        assertFalse(hasPublicMethod(ZlinkException.class, "errno"));
        assertFalse(hasPublicMethod(ZlinkException.class, "errorCode"));
        assertFalse(isPublicClass("systems.zlink.contracts.sockets.SubscribeHandler"));
        assertFalse(isPublicClass("systems.zlink.contracts.sockets.SocketOption"));
        assertFalse(isPublicClass("systems.zlink.contracts.errors.ErrorCode"));
        assertFalse(isPublicClass("systems.zlink.contracts.sockets.RequestReplyCallback"));
        assertFalse(isPublicClass("systems.zlink.contracts.sockets.SocketPollSet"));
        assertFalse(isPublicClass("systems.zlink.contracts.sockets.DisconnectReason"));
        assertFalse(isPublicClass("systems.zlink.contracts.errors.ProtocolError"));
        assertFalse(isExportedPackage("systems.zlink.runtime.core"));
        assertFalse(isExportedPackage("systems.zlink.runtime.eventing"));
        assertFalse(isExportedPackage("systems.zlink.runtime.nativeapi"));
        assertFalse(isExportedPackage("systems.zlink.runtime.service.discovery"));
        assertFalse(isExportedPackage("systems.zlink.runtime.service.registry"));
        assertFalse(isExportedPackage("systems.zlink.runtime.service.spot"));
        assertFalse(isExportedPackage("systems.zlink.runtime.sockets"));
        assertFalse(isPublicClass("systems.zlink.contracts.sockets.StreamDispatchMode"));
        assertTrue(isPublicClass("systems.zlink.contracts.messaging.SubscriptionEntry"));
        assertTrue(isPublicClass("systems.zlink.contracts.core.ZlinkVersion"));
        assertTrue(isPublicClass("systems.zlink.contracts.sockets.SocketType"));
        assertFalse(hasPublicMethod(SubSocketOptions.class, "subscriptionAt",
            int.class));
        assertTrue(hasPublicMethod(SubSocket.class, "subscriptionAt",
            int.class));
        assertFalse(hasPublicMethod(Spot.class, "options"));
        assertFalse(hasPublicMethod(Poller.class, "add", ZlinkTimer.class));
        assertTrue(hasPublicMethod(Poller.class, "add", ZlinkTimer.class,
            long.class));
        assertFalse(hasPublicMethod(Poller.class, "add", ZlinkTimer.class,
            Object.class));
        assertTrue(hasPublicMethod(Poller.class, "remove", ZlinkTimer.class));
        assertTrue(hasPublicMethod(Poller.class, "add", Spot.class,
            long.class, PollEventFlags[].class));
        assertFalse(hasPublicMethod(Poller.class, "add", Spot.class,
            PollEventFlags[].class));
        assertFalse(hasPublicMethod(Poller.class, "add", Spot.class,
            Object.class, PollEventFlags[].class));
        assertTrue(hasPublicMethod(Poller.class, "wait", PollEvents.class,
            Duration.class));
        assertFalse(hasPublicMethod(Poller.class, "wait", Duration.class));
        assertFalse(hasPublicMethod(Poller.class, "wait", int.class,
            Duration.class));
        assertFalse(hasPublicMethod(Poller.class, "wait", List.class,
            Duration.class));
        assertTrue(hasPublicMethod(Poller.class, "modify", Spot.class,
            PollEventFlags[].class));
        assertTrue(hasPublicMethod(Poller.class, "remove", Spot.class));
        assertFalse(hasPublicMethod(Poller.class, "readyZlinkTimer", int.class));
        assertTrue(hasPublicMethod(systems.zlink.contracts.eventing.SocketMonitor.class,
            "recv", RecvFlags.class));
    }

    @Test
    public void actorRouteSnapshotsExposeCurrentSpotRidAndKind() {
        RoutingId nodeRid = RoutingId.from(new byte[] {1});
        ActorRef actor = new ActorRef(nodeRid, "actor", 1L);
        RoutingId currentSpotRid = RoutingId.from(new byte[] {2});

        ActorRoute route = new ActorRoute(actor, currentSpotRid,
            SpotKind.USER);
        assertEquals(currentSpotRid, route.currentSpotRid());
        assertEquals(SpotKind.USER, route.currentSpotKind());

        SpotNodeActorEntry entry = new SpotNodeActorEntry(actor,
            currentSpotRid, SpotKind.ENTRY, true, 0, 0L);
        assertEquals(currentSpotRid, entry.currentSpotRid());
        assertEquals(SpotKind.ENTRY, entry.currentSpotKind());
    }

    @Test
    public void actorJoinBuilderSurfaceMatchesJavaSpec() throws Exception {
        assertEquals(ActorJoinSubmitOperation.class,
            ActorJoinOperation.class.getMethod("message", Message.class)
                .getReturnType());
        assertEquals(ActorJoinSubmitOperation.class,
            ActorJoinSubmitOperation.class.getMethod("message", Message.class)
                .getReturnType());
        assertEquals(ActorJoinSubmitOperation.class,
            ActorJoinSubmitOperation.class.getMethod("timeout", Duration.class)
                .getReturnType());
        assertEquals(ActorJoinCallbackSubmitOperation.class,
            ActorJoinSubmitOperation.class.getMethod("flags", SendFlags.class)
                .getReturnType());
        assertEquals(ActorJoinCallbackSubmitOperation.class,
            ActorJoinCallbackSubmitOperation.class.getMethod("message", Message.class)
                .getReturnType());
        assertEquals(ActorJoinCallbackSubmitOperation.class,
            ActorJoinCallbackSubmitOperation.class.getMethod("timeout", Duration.class)
                .getReturnType());
        assertEquals(ActorJoinCallbackSubmitOperation.class,
            ActorJoinCallbackSubmitOperation.class.getMethod("flags", SendFlags.class)
                .getReturnType());
        assertFalse(hasPublicMethod(ActorJoinCallbackSubmitOperation.class,
            "submitAsync"));

        assertEquals(ActorJoinEntrySpotOperation.class,
            ActorJoinEntrySpotOperation.class.getMethod("timeout", Duration.class)
                .getReturnType());
        assertEquals(ActorJoinEntrySpotOperation.class,
            SpotNode.class.getMethod("joinActorEntrySpot", ActorRef.class,
                RoutingId.class).getReturnType());
    }

    @Test
    public void routerReplyWithFlagsFailsExplicitlyWhenCoreLacksSupport()
      throws Exception {
        TestSupport.assumeNative();

        Context ctx = Zlink.createContext();
        RouterSocket routerSocket = ctx.createRouterSocket();
        DealerSocket dealerSocket = ctx.createDealerSocket();
        try {
            String endpoint = TestSupport.inprocEndpoint("request-reply-flags");
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);

            CompletableFuture<List<Message>> future;
            try (Message request = Message.from("ping")) {
                future = dealerSocket.request()
                    .message(request)
                    .timeout(Duration.ofMillis(50))
                    .submitAsync()
                    .toCompletableFuture();
            }

            try (systems.zlink.contracts.messaging.Received received = new systems.zlink.contracts.messaging.Received()) {


                routerSocket.recv(received, systems.zlink.contracts.sockets.RecvFlags.NONE);
                ZlinkSubmitException submitException = assertThrows(
                    ZlinkSubmitException.class,
                    () -> received.reply()
                        .message(Message.from("pong"))
                        .flags(SendFlags.DONT_WAIT)
                        .submit());
                assertEquals(SubmitResult.NOT_SUPPORTED,
                    submitException.getResult());
            }

            ExecutionException completion = assertThrows(ExecutionException.class,
                () -> future.get(1, TimeUnit.SECONDS));
            assertTrue(completion.getCause() instanceof systems.zlink.contracts.errors.ZlinkRequestException);
        } finally {
            try {
                dealerSocket.close();
            } catch (RuntimeException ignored) {
            }
            try {
                routerSocket.close();
            } catch (RuntimeException ignored) {
            }
            try {
                ctx.shutdown();
            } catch (RuntimeException ignored) {
            }
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
    public void streamSocketDoesNotExposeLegacyStreamOrConnectSurface()
      throws Exception {
        assertFalse(hasPublicMethod(StreamSocket.class, "attachStream"));
        assertFalse(hasPublicMethod(StreamSocket.class, "streamSend"));
        assertFalse(hasPublicMethod(StreamSocket.class, "onReceive"));
        assertTrue(hasPublicMethod(StreamSocket.class, "onPacket",
            systems.zlink.contracts.sockets.StreamPacketHandler.class));
        assertFalse(hasPublicMethod(StreamSocket.class, "onPacketNative"));
        assertFalse(hasPublicMethod(StreamSocket.class, "onFramedPacket"));
        assertFalse(hasPublicMethod(StreamSocket.class, "onFramedPacketNative"));
        assertFalse(hasPublicMethod(StreamSocket.class, "sendCopied",
            int.class, MEMORY_SEGMENT_CLASS, int.class,
            SendFlags.class));
        assertFalse(hasPublicMethod(StreamSocket.class, "send",
            int.class, MEMORY_SEGMENT_CLASS, int.class,
            SendFlags.class));
        assertFalse(hasPublicMethod(StreamSocket.class, "attachStreamRaw"));
        assertFalse(hasPublicMethod(StreamSocket.class, "connect"));
        assertFalse(hasPublicMethod(StreamSocket.class, "attachDiscovery"));
        assertEquals(void.class, StreamSocket.class
            .getMethod("attachActorGateway", SpotNode.class)
            .getReturnType());
        assertEquals(SendOperation.class, StreamSocket.class
            .getMethod("send", RoutingId.class).getReturnType());
        assertEquals(ActorBindOperation.class, StreamSocket.class
            .getMethod("bindActor", RoutingId.class, ActorRef.class)
            .getReturnType());
        assertEquals(ActorUnbindOperation.class, StreamSocket.class
            .getMethod("unbindActor", RoutingId.class, String.class)
            .getReturnType());
        assertEquals(SendOperation.class, StreamSocket.class
            .getMethod("sendBoundActor", RoutingId.class, String.class)
            .getReturnType());
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
    public void discoveryAndSpotIdentitySurfaceWorksWithTypedContracts() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             Discovery discovery = ctx.createDiscovery(AutoConnectType.CLIENT_SERVER, "svc");
             SpotNode node = ctx.createSpotNode()) {
            RoutingId nodeRid = RoutingId.from(
              "spot-node".getBytes(StandardCharsets.UTF_8));
            node.setRoutingId(nodeRid);
            assertArrayEquals(nodeRid.toBytes(), node.getRoutingId().toBytes());

            try (Spot spot = node.createSpot()) {
                RoutingId spotRid = RoutingId.from(
                  "spot-self".getBytes(StandardCharsets.UTF_8));
                spot.setRoutingId(spotRid);
                assertArrayEquals(spotRid.toBytes(), spot.getRoutingId().toBytes());
            }

            RoutingId roomRid = RoutingId.from(
              "java-room".getBytes(StandardCharsets.UTF_8));
            SpotNode.SpotGetOrCreateResult first = node.getOrCreateSpot(roomRid);
            SpotNode.SpotGetOrCreateResult second = node.getOrCreateSpot(roomRid);
            try (Spot firstSpot = first.spot(); Spot secondSpot = second.spot()) {
                assertTrue(first.created());
                assertFalse(second.created());
                assertArrayEquals(roomRid.toBytes(), firstSpot.getRoutingId().toBytes());
                assertArrayEquals(roomRid.toBytes(), secondSpot.getRoutingId().toBytes());
            }
        }
    }

    @Test
    public void rawOptionSurfaceIsHiddenAndTypedOptionsRemain() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             PairSocket pair = ctx.createPairSocket();
             PubSocket pub = ctx.createPubSocket();
             SubSocket sub = ctx.createSubSocket();
             StreamSocket stream = ctx.createStreamSocket();
             XPubSocket xpub = ctx.createXPubSocket();
             XSubSocket xsub = ctx.createXSubSocket()) {
            assertFalse(hasPublicMethod(PairSocket.class, "setOption"));
            assertFalse(hasPublicMethod(PairSocket.class, "getOption"));
            assertFalse(hasPublicMethod(PairSocket.class, "setSockOpt"));
            assertFalse(hasPublicMethod(PairSocket.class, "getSockOptInt"));
            assertFalse(hasPublicMethod(SocketMonitor.class, "setOption"));
            assertFalse(hasPublicMethod(SocketMonitor.class, "sendHighWaterMark"));
            assertFalse(hasPublicMethod(SocketMonitor.class, "receiveHighWaterMark"));
            assertFalse(hasPublicMethod(Context.class, "handle"));
            assertFalse(hasPublicMethod(Discovery.class, "handle"));
            assertFalse(hasPublicMethod(Discovery.class, "setTlsServer",
                String.class, String.class, boolean.class));
            assertFalse(hasPublicMethod(Spot.class, "handle"));
            assertFalse(hasPublicMethod(Spot.class, "monitorOpen", int.class));
            assertFalse(hasPublicMethod(systems.zlink.contracts.service.spot.SpotNode.class,
                "monitorOpen", int.class));
            assertTrue(hasPublicMethod(SocketMonitor.class, "recv",
                RecvFlags.class));
            assertTrue(hasPublicMethod(PairSocket.class, "send"));
            assertFalse(hasPublicMethod(PairSocket.class, "recv",
                RecvFlags.class));
            assertTrue(hasPublicMethod(PairSocket.class, "recv",
                Received.class, RecvFlags.class));
            assertTrue(hasPublicMethod(PubSocket.class, "publish", String.class));
            assertTrue(hasPublicMethod(SubSocket.class, "subscribe",
                TopicMessage.class, RecvFlags.class));
            assertFalse(hasPublicMethod(Message.class, "dataSegment"));
            assertFalse(hasPublicMethod(Message.class, "dataSegment", int.class));
            assertFalse(hasPublicMethod(Message.class, "copyTo",
                MEMORY_SEGMENT_CLASS));
            assertFalse(hasPublicMethod(Message.class, "moveTo",
                MEMORY_SEGMENT_CLASS));
            assertFalse(hasPublicMethod(Message.class, "fromMsgVector",
                MEMORY_SEGMENT_CLASS, long.class));
            assertFalse(hasPublicMethod(Message.class, "fromOwnedMsgVector",
                MEMORY_SEGMENT_CLASS, long.class));
            assertFalse(hasPublicMethod(Message.class, "property",
                String.class));
            assertTrue(hasPublicMethod(Message.class, "getProperty",
                String.class));
            assertTrue(hasPublicMethod(Message.class, "refCount"));
            assertFalse(hasPublicMethod(XPubSocket.class, "subscriptionEvent"));
            assertFalse(hasPublicMethod(PairSocket.class, "sendNoWaitResult", Message.class));
            assertFalse(hasPublicMethod(RouterSocket.class, "send",
                byte[].class, Message.class, SendFlag.class));
            assertFalse(hasPublicMethod(RouterSocket.class, "sendInternal",
                RoutingId.class, Message.class, SendFlags.class));
            assertFalse(hasPublicMethod(RouterSocket.class, "sendInternal",
                RoutingId.class, List.class, SendFlags.class));
            assertFalse(hasPublicMethod(RouterSocket.class, "sendNoWaitResult",
                RoutingId.class, Message.class));
            assertFalse(hasPublicMethod(RouterSocket.class, "sendNoWaitResult",
                RoutingId.class, List.class));
            assertFalse(hasPublicMethod(RouterSocket.class,
                "sendToSpotInternal", RoutingId.class, RoutingId.class,
                List.class, SendFlags.class));
            assertFalse(hasPublicMethod(PairSocket.class, "recvNoWait"));
            assertFalse(hasPublicMethod(SocketMonitor.class, "recvNoWait"));
            assertFalse(hasPublicMethod(PairSocket.class, "trySend", Message.class));
            assertFalse(hasPublicMethod(PairSocket.class, "trySend", List.class));
            assertFalse(hasPublicMethod(PairSocket.class, "tryRecv"));
            assertFalse(hasPublicMethod(DealerSocket.class, "trySend", Message.class));
            assertFalse(hasPublicMethod(DealerSocket.class, "trySend", List.class));
            assertFalse(hasPublicMethod(DealerSocket.class, "tryRecv"));
            assertFalse(hasPublicMethod(RouterSocket.class, "trySend", RoutingId.class, Message.class));
            assertFalse(hasPublicMethod(RouterSocket.class, "trySend", RoutingId.class, List.class));
            assertFalse(hasPublicMethod(RouterSocket.class, "tryRecv"));
            assertFalse(hasPublicMethod(StreamSocket.class, "trySend", int.class, Message.class));
            assertFalse(hasPublicMethod(StreamSocket.class, "trySend", RoutingId.class, Message.class));
            assertFalse(hasPublicMethod(StreamSocket.class, "tryRecv"));
            assertFalse(hasPublicMethod(PubSocket.class, "publishNoWaitResult",
                String.class, Message.class));
            assertFalse(hasPublicMethod(SubSocket.class, "subscribeNoWait"));
            assertFalse(hasPublicMethod(SubSocket.class, "onSubscribe"));
            assertFalse(hasPublicMethod(XSubSocket.class, "onSubscribe"));
            assertFalse(hasPublicMethod(XPubSocket.class,
                "tryReceiveSubscriptionEvent"));
            assertEquals(PubSocketOptions.class, pub.options().getClass());
            assertTrue(hasPublicMethod(PubSocketOptions.class, "verbose"));
            assertTrue(hasPublicMethod(PubSocketOptions.class, "verboser"));
            assertTrue(hasPublicMethod(PubSocketOptions.class, "noDrop"));
            assertTrue(hasPublicMethod(PubSocketOptions.class, "manual"));
            assertTrue(hasPublicMethod(PubSocketOptions.class, "manualLastValue"));
            assertTrue(hasPublicMethod(PubSocketOptions.class,
                "approveSubscribe", RoutingId.class));
            assertTrue(hasPublicMethod(PubSocketOptions.class,
                "rejectSubscribe", RoutingId.class));
            assertTrue(hasPublicMethod(PubSocketOptions.class,
                "welcomeMessage"));
            assertFalse(hasPublicMethod(PubSocketOptions.class, "welcomeMsg"));
            assertTrue(hasPublicMethod(PubSocketOptions.class, "topicsCount"));
            assertFalse(hasPublicMethod(PubSocket.class, "advancedOptions"));
            assertFalse(hasPublicMethod(XPubSocket.class, "advancedOptions"));
            assertEquals(PubSocketOptions.class, xpub.options().getClass());
            assertDoesNotThrow(() -> xpub.options().manual(true));
            assertTrue(xpub.options().manual());
            assertDoesNotThrow(() -> xpub.options().verbose(true));
            assertDoesNotThrow(() -> xpub.options().verboser(true));
            assertDoesNotThrow(() -> xpub.options().noDrop(true));
            assertEquals(SubSocketOptions.class, sub.options().getClass());
            assertTrue(hasPublicMethod(SubSocketOptions.class, "topicsCount"));
            assertEquals(0, sub.options().topicsCount());
            assertEquals(SubSocketOptions.class, xsub.options().getClass());
            assertEquals(0, xsub.options().topicsCount());
            assertDoesNotThrow(() -> stream.options().notify(true));
            assertTrue(stream.options().notifyEnabled());
            assertTrue(hasPublicMethod(PairSocket.class, "options"));
            assertEquals(CommonSocketOptions.class, pair.options().getClass());
            assertEquals(SubmitRetryMode.OFF, pair.options().submitRetryMode());
            assertEquals(Duration.ZERO, pair.options().submitRetryTimeout());
            assertEquals(0, pair.options().submitRetryAttempts());
            assertDoesNotThrow(() ->
                pair.options().submitRetryMode(SubmitRetryMode.LOCAL_FAILURE));
            assertDoesNotThrow(() ->
                pair.options().submitRetryTimeout(Duration.ofMillis(42)));
            assertDoesNotThrow(() -> pair.options().submitRetryAttempts(2));
            assertEquals(SubmitRetryMode.LOCAL_FAILURE,
                pair.options().submitRetryMode());
            assertEquals(Duration.ofMillis(42),
                pair.options().submitRetryTimeout());
            assertEquals(2, pair.options().submitRetryAttempts());
            assertFalse(hasPublicMethod(CommonSocketOptions.class,
                "autoHwmMessageUnitBytes"));
        }
    }

    @Test
    public void receivedAndMemberPeerSurfaceMatchesJavaSpec() {
        assertFalse(Iterable.class.isAssignableFrom(Received.class));
        assertFalse(hasPublicMethod(Received.class, "iterator"));
        assertFalse(hasPublicMethod(Received.class, "routingIdOrNull"));
        assertFalse(hasPublicMethod(Received.class, "routingIdOrThrow"));
        assertFalse(hasPublicMethod(Received.class, "spotRidOrNull"));
        assertFalse(hasPublicMethod(systems.zlink.contracts.eventing.MonitorStatus.class,
            "fromNative", MEMORY_SEGMENT_CLASS));
        assertFalse(hasPublicMethod(
            systems.zlink.contracts.service.registry.MemberPeerEntry.class,
            "fromNative", MEMORY_SEGMENT_CLASS));
        assertEquals(7, MemberPeerEntry.class.getRecordComponents().length);
        assertEquals("weight",
            MemberPeerEntry.class.getRecordComponents()[6].getName());
    }

    @Test
    public void sendAndRecvUseCanonicalNonBlockingSurface() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             PairSocket server = ctx.createPairSocket();
             PairSocket client = ctx.createPairSocket()) {
            String endpoint = TestSupport.inprocEndpoint("socket-try-contract");
            server.bind(endpoint);
            client.connect(endpoint);

            try (systems.zlink.contracts.messaging.Received probe = new systems.zlink.contracts.messaging.Received()) {
                assertFalse(server.recv(probe, RecvFlags.DONT_WAIT));
            }
            try (Message outbound = Message.from("pair-try")) {
                assertTrue(client.send().message(outbound).flags(SendFlags.DONT_WAIT).submit());
            }
            try (systems.zlink.contracts.messaging.Received received = new systems.zlink.contracts.messaging.Received()) {
                assertTrue(server.recv(received, RecvFlags.DONT_WAIT));
                assertEquals(List.of("pair-try"),
                    List.of(received.singlePartOrThrow().toUtf8String()));
            }
        }
    }

    @Test
    public void attachDiscoveryGatesManualPeerApisAndSocketClose() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             Discovery discovery = ctx.createDiscovery(AutoConnectType.CLIENT_SERVER, "socket-svc")) {
            DealerSocket dealer = ctx.createDealerSocket();
            dealer.attachDiscovery(discovery);

            ZlinkException connectError = assertThrows(ZlinkException.class,
                () -> dealer.connect("tcp://127.0.0.1:39001"));
            assertEquals(ERRNO_EFSM, connectError.getNativeErrno());

            ZlinkException disconnectError = assertThrows(ZlinkException.class,
                () -> dealer.disconnect("tcp://127.0.0.1:39001"));
            assertEquals(ERRNO_EFSM, disconnectError.getNativeErrno());

            ZlinkException unbindError = assertThrows(ZlinkException.class,
                () -> dealer.unbind("tcp://127.0.0.1:39001"));
            assertEquals(ERRNO_EFSM, unbindError.getNativeErrno());

            ZlinkException closeError = assertThrows(ZlinkException.class,
                dealer::close);
            assertEquals(ERRNO_EFSM, closeError.getNativeErrno());
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

    private static boolean isPublicClass(String className) {
        try {
            return Modifier.isPublic(Class.forName(className).getModifiers());
        } catch (ClassNotFoundException ex) {
            return false;
        }
    }

    private static Class<?> classNamed(String className) {
        try {
            return Class.forName(className);
        } catch (ClassNotFoundException ex) {
            throw new AssertionError("missing class " + className, ex);
        }
    }

    private static boolean isExportedPackage(String packageName) {
        try {
            String moduleInfo = Files.readString(Path.of(
                "src/main/java/module-info.java"));
            return moduleInfo.contains("exports " + packageName + ";");
        } catch (Exception ex) {
            throw new AssertionError("failed to read module-info.java", ex);
        }
    }

    private static ExecutorService daemonExecutor(String name) {
        return Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, name);
            thread.setDaemon(true);
            return thread;
        });
    }

}
