package dev.kairoscode.zlink.contract;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorSocket;
import dev.kairoscode.zlink.PairSocket;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.PubSocketOptions;
import dev.kairoscode.zlink.RecvException;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.Received;
import dev.kairoscode.zlink.RequestResult;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.SubmitResult;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.ServiceMonitorEventMask;
import dev.kairoscode.zlink.service.discovery.DiscoveryDealerPeerMode;
import dev.kairoscode.zlink.service.registry.ServiceType;
import dev.kairoscode.zlink.StreamSocket;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.SubSocketOptions;
import dev.kairoscode.zlink.TestSupport;
import dev.kairoscode.zlink.TopicMessage;
import dev.kairoscode.zlink.XPubSocket;
import dev.kairoscode.zlink.XSubSocket;
import dev.kairoscode.zlink.Zlink;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.service.spot.SpotNode;
import dev.kairoscode.zlink.service.registry.Registry;
import dev.kairoscode.zlink.service.spot.Spot;
import java.lang.foreign.MemorySegment;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
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
    public void requestReplyWrapperSupportsDealerRouterRoundTrip() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket routerSocket = new RouterSocket(ctx);
             DealerSocket dealerSocket = new DealerSocket(ctx);
             ExecutorService serverExecutor = daemonExecutor("zlink-socket-contract")) {
            String endpoint = TestSupport.inprocEndpoint("request-reply");
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);

            CompletableFuture<Void> server = CompletableFuture.runAsync(() -> {
                try (Received received = routerSocket.recv()) {
                    assertArrayEquals("ping".getBytes(StandardCharsets.UTF_8),
                        received.singlePartOrThrow().toByteArray());
                    assertTrue(received.routingId().isPresent());
                    assertTrue(received.requestSeq().isPresent());
                    assertTrue(received.requestSeq().orElseThrow() != 0L);
                    received.reply(List.of(Message.copyOfUtf8("pong")));
                }
            }, serverExecutor);

            try (Message request = Message.copyOfUtf8("ping")) {
                List<Message> reply = dealerSocket.request(request,
                    Duration.ofSeconds(2)).get(2, TimeUnit.SECONDS);
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
    public void requestReplyWrapperPreservesDataReceiveSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket routerSocket = new RouterSocket(ctx);
             DealerSocket dealerSocket = new DealerSocket(ctx)) {
            String endpoint = TestSupport.inprocEndpoint("request-reply-data");
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);

            dealerSocket.send(List.of(Message.copyOfUtf8("plain-data")));

            try (Received received = routerSocket.recv()) {
                assertArrayEquals("plain-data".getBytes(StandardCharsets.UTF_8),
                    received.singlePartOrThrow().toByteArray());
            }
        }
    }

    @Test
    public void requestReplyCallbackCompletesBeforeSocketClose() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket routerSocket = new RouterSocket(ctx);
             DealerSocket dealerSocket = new DealerSocket(ctx);
             ExecutorService serverExecutor =
                 daemonExecutor("zlink-socket-contract-callback")) {
            String endpoint = TestSupport.inprocEndpoint("request-reply-callback");
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);

            CompletableFuture<Void> server = CompletableFuture.runAsync(() -> {
                try (Received received = routerSocket.recv()) {
                    received.reply(List.of(Message.copyOfUtf8("pong-callback")));
                }
            }, serverExecutor);

            CountDownLatch done = new CountDownLatch(1);
            AtomicReference<List<Message>> replyRef = new AtomicReference<>();
            AtomicReference<RequestResult> resultRef = new AtomicReference<>();
            AtomicReference<Throwable> errorRef = new AtomicReference<>();
            try (Message request = Message.copyOfUtf8("ping-callback")) {
                dealerSocket.request(request, (result, reply) -> {
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

        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(
               dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY);
             var subMonitor = sub.monitorOpen(
               dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY)) {
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
                assertEquals("socket-topic", received.topic());
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
            RoutingId routerRid = RoutingId.fromBytes("router-self".getBytes(StandardCharsets.UTF_8));
            router.setRoutingId(routerRid);
            assertArrayEquals(routerRid.toBytes(), router.routingId().toBytes());

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

        try (Context ctx = new Context();
             Registry registry = new Registry(ctx);
             Discovery discovery = new Discovery(ctx, ServiceType.SOCKET,
               "svc-tls");
             PairSocket socket = new PairSocket(ctx)) {
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
                dev.kairoscode.zlink.MonitorEventType[].class));
            assertFalse(hasPublicMethod(PairSocket.class, "monitorOpen",
                int.class));
            assertTrue(hasPublicMethod(Discovery.class, "monitorOpen"));
            assertTrue(hasPublicMethod(Discovery.class, "monitorOpen",
                ServiceMonitorEventMask[].class));
            assertFalse(hasPublicMethod(Discovery.class, "monitorOpen",
                int.class));

            String cert = Path.of("tests/certs/server.crt").toAbsolutePath().toString();
            String key = Path.of("tests/certs/server.key").toAbsolutePath().toString();
            String ca = Path.of("tests/certs/ca.crt").toAbsolutePath().toString();
            assertDoesNotThrow(() -> registry.setTlsServer(cert, key, true));
            assertDoesNotThrow(() -> registry.setTlsClient(ca, "localhost", true));
            assertDoesNotThrow(() -> discovery.setTlsClient(ca, "localhost", true));
            assertDoesNotThrow(() -> {
                try (var monitor = socket.monitorOpen()) {
                    assertTrue(monitor.snapshot().sndPendingMsgs() >= 0L);
                }
            });
        }
    }

    @Test
    public void routedAndLegacySurfaceMatchesJavaSpec() {
        assertTrue(hasPublicMethod(RouterSocket.class, "sendToSpot",
            RoutingId.class, RoutingId.class, Message.class));
        assertTrue(hasPublicMethod(RouterSocket.class, "requestToSpot",
            RoutingId.class, RoutingId.class, Message.class));
        assertTrue(hasPublicMethod(RouterSocket.class, "replyToSpot",
            RoutingId.class, RoutingId.class, long.class, Message.class));
        assertFalse(hasPublicMethod(RouterSocket.class, "recvSpot"));
        assertFalse(hasPublicMethod(RouterSocket.class, "recvSpot",
            RecvFlags.class));
        assertFalse(hasPublicMethod(RouterSocket.class, "onSpotReceive"));

        assertTrue(hasPublicMethod(Spot.class, "replyToSpot",
            RoutingId.class, RoutingId.class, long.class, Message.class));
        assertTrue(hasPublicMethod(Spot.class, "replyToRouter",
            RoutingId.class, long.class, Message.class));
        assertTrue(hasPublicMethod(Spot.class, "recvRouted"));
        assertTrue(hasPublicMethod(Spot.class, "onRoutedReceive",
            dev.kairoscode.zlink.SpotRoutedHandler.class));
        assertTrue(hasPublicMethod(Spot.class, "onDispatchEvent",
            dev.kairoscode.zlink.SpotDispatchEventHandler.class));
        assertTrue(hasPublicMethod(Spot.class, "setRoutingId",
            RoutingId.class));
        assertTrue(hasPublicMethod(Spot.class, "routingId"));
        assertTrue(hasPublicMethod(SpotNode.class, "setRoutingId",
            RoutingId.class));
        assertTrue(hasPublicMethod(SpotNode.class, "routingId"));
        assertTrue(hasPublicMethod(Discovery.class, "resolveSpot",
            RoutingId.class));
        assertTrue(hasPublicMethod(Discovery.class, "setDealerPeerMode",
            DiscoveryDealerPeerMode.class));

        assertFalse(hasPublicMethod(XPubSocket.class, "onSubscribe",
            dev.kairoscode.zlink.SubscribeHandler.class));
        assertFalse(hasPublicMethod(SubSocket.class, "onSubscribe",
            dev.kairoscode.zlink.SubscribeHandler.class));
        assertFalse(hasPublicMethod(XSubSocket.class, "onSubscribe",
            dev.kairoscode.zlink.SubscribeHandler.class));
        assertFalse(hasPublicMethod(Zlink.class, "errno"));
        assertFalse(hasPublicMethod(ZlinkException.class, "errno"));
        assertFalse(hasPublicMethod(ZlinkException.class, "errorCode"));
        assertFalse(isPublicClass("dev.kairoscode.zlink.ErrorCode"));
        assertFalse(isPublicClass("dev.kairoscode.zlink.RequestReplyCallback"));
        assertFalse(isPublicClass("dev.kairoscode.zlink.SocketPollSet"));
        assertFalse(isPublicClass("dev.kairoscode.zlink.DisconnectReason"));
        assertFalse(isPublicClass("dev.kairoscode.zlink.ProtocolError"));
        assertFalse(isPublicClass("dev.kairoscode.zlink.StreamDispatchMode"));
        assertFalse(isPublicClass("dev.kairoscode.zlink.SubscriptionEntry"));
        assertFalse(isPublicClass("dev.kairoscode.zlink.ZlinkVersion"));
        assertFalse(isPublicClass("dev.kairoscode.zlink.SocketType"));
        assertFalse(hasPublicMethod(dev.kairoscode.zlink.MonitorSocket.class,
            "recv", RecvFlags.class));
        assertFalse(hasPublicMethod(dev.kairoscode.zlink.ServiceMonitor.class,
            "recv", RecvFlags.class));
    }

    @Test
    public void routerReplyWithFlagsFailsExplicitlyWhenCoreLacksSupport()
      throws Exception {
        TestSupport.assumeNative();

        Context ctx = new Context();
        RouterSocket routerSocket = new RouterSocket(ctx);
        DealerSocket dealerSocket = new DealerSocket(ctx);
        try {
            String endpoint = TestSupport.inprocEndpoint("request-reply-flags");
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);

            CompletableFuture<List<Message>> future;
            try (Message request = Message.copyOfUtf8("ping")) {
                future = dealerSocket.request(request, Duration.ofMillis(50));
            }

            try (Received received = routerSocket.recv()) {
                SubmitException submitException = assertThrows(
                    SubmitException.class,
                    () -> received.reply(List.of(Message.copyOfUtf8("pong")),
                        SendFlags.DONT_WAIT));
                assertEquals(SubmitResult.NOT_SUPPORTED,
                    submitException.getResult());
            }

            ExecutionException completion = assertThrows(ExecutionException.class,
                () -> future.get(1, TimeUnit.SECONDS));
            assertTrue(completion.getCause() instanceof dev.kairoscode.zlink.RequestException);
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
    public void streamSocketDoesNotExposeLegacyStreamOrConnectSurface() {
        assertFalse(hasPublicMethod(StreamSocket.class, "attachStream"));
        assertFalse(hasPublicMethod(StreamSocket.class, "streamSend"));
        assertFalse(hasPublicMethod(StreamSocket.class, "onReceive"));
        assertTrue(hasPublicMethod(StreamSocket.class, "onPacket",
            dev.kairoscode.zlink.StreamPacketHandler.class));
        assertFalse(hasPublicMethod(StreamSocket.class, "attachStreamRaw"));
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
    public void discoveryAndSpotIdentitySurfaceWorksWithTypedContracts() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Discovery discovery = new Discovery(ctx, ServiceType.SOCKET, "svc");
             SpotNode node = new SpotNode(ctx)) {
            assertDoesNotThrow(() ->
              discovery.setDealerPeerMode(DiscoveryDealerPeerMode.ROUTER));
            assertDoesNotThrow(() ->
              discovery.setDealerPeerMode(DiscoveryDealerPeerMode.DEALER));

            RoutingId nodeRid = RoutingId.fromBytes(
              "spot-node".getBytes(StandardCharsets.UTF_8));
            node.setRoutingId(nodeRid);
            assertArrayEquals(nodeRid.toBytes(), node.routingId().toBytes());

            try (Spot spot = node.createSpot()) {
                RoutingId spotRid = RoutingId.fromBytes(
                  "spot-self".getBytes(StandardCharsets.UTF_8));
                spot.setRoutingId(spotRid);
                assertArrayEquals(spotRid.toBytes(), spot.routingId().toBytes());
            }
        }
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
            assertFalse(hasPublicMethod(MonitorSocket.class, "sendHighWaterMark"));
            assertFalse(hasPublicMethod(MonitorSocket.class, "receiveHighWaterMark"));
            assertFalse(hasPublicMethod(Context.class, "handle"));
            assertFalse(hasPublicMethod(Discovery.class, "handle"));
            assertFalse(hasPublicMethod(Discovery.class, "setTlsServer",
                String.class, String.class, boolean.class));
            assertFalse(hasPublicMethod(Spot.class, "handle"));
            assertFalse(hasPublicMethod(Spot.class, "monitorOpen", int.class));
            assertFalse(hasPublicMethod(dev.kairoscode.zlink.service.spot.SpotNode.class,
                "monitorOpen", int.class));
            assertFalse(hasPublicMethod(MonitorSocket.class, "recv",
                RecvFlags.class));
            assertTrue(hasPublicMethod(PairSocket.class, "send", Message.class,
                SendFlags.class));
            assertTrue(hasPublicMethod(PairSocket.class, "recv",
                RecvFlags.class));
            assertTrue(hasPublicMethod(PubSocket.class, "publish", String.class,
                Message.class, SendFlags.class));
            assertTrue(hasPublicMethod(SubSocket.class, "subscribe",
                RecvFlags.class));
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
            assertFalse(hasPublicMethod(Message.class, "property",
                String.class));
            assertTrue(hasPublicMethod(Message.class, "getProperty",
                String.class));
            assertTrue(hasPublicMethod(Message.class, "refCount"));
            assertFalse(hasPublicMethod(XPubSocket.class, "subscriptionEvent"));
            assertFalse(hasPublicConstructor(ServiceMonitor.class,
                MemorySegment.class));
            assertFalse(hasPublicMethod(PairSocket.class, "sendNoWaitResult", Message.class));
            assertFalse(hasPublicMethod(PairSocket.class, "recvNoWait"));
            assertFalse(hasPublicMethod(MonitorSocket.class, "recvNoWait"));
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
            assertFalse(hasPublicMethod(SubSocket.class, "onSubscribe",
                dev.kairoscode.zlink.SubscribeHandler.class));
            assertFalse(hasPublicMethod(XSubSocket.class, "onSubscribe",
                dev.kairoscode.zlink.SubscribeHandler.class));
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
            assertTrue(hasPublicMethod(PubSocketOptions.class, "welcomeMsg"));
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
            assertDoesNotThrow(() -> pair.options().recvTimeout(Duration.ofMillis(10)));
            assertEquals(Duration.ofMillis(10), pair.options().recvTimeout());
            assertDoesNotThrow(() -> pair.options().sendTimeout(Duration.ofMillis(20)));
            assertEquals(Duration.ofMillis(20), pair.options().sendTimeout());
        }
    }

    @Test
    public void sendAndRecvUseCanonicalNonBlockingSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             PairSocket server = new PairSocket(ctx);
             PairSocket client = new PairSocket(ctx)) {
            String endpoint = TestSupport.inprocEndpoint("socket-try-contract");
            server.bind(endpoint);
            client.connect(endpoint);

            assertNull(server.recv(RecvFlags.DONT_WAIT));
            try (Message outbound = Message.copyOfUtf8("pair-try")) {
                assertTrue(client.send(outbound, SendFlags.DONT_WAIT));
            }
            try (var received = server.recv(RecvFlags.DONT_WAIT)) {
                assertNotNull(received);
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
            assertEquals(ERRNO_EFSM, connectError.getInternalErrno());

            ZlinkException disconnectError = assertThrows(ZlinkException.class,
                () -> dealer.disconnect("tcp://127.0.0.1:39001"));
            assertEquals(ERRNO_EFSM, disconnectError.getInternalErrno());

            ZlinkException unbindError = assertThrows(ZlinkException.class,
                () -> dealer.unbind("tcp://127.0.0.1:39001"));
            assertEquals(ERRNO_EFSM, unbindError.getInternalErrno());

            ZlinkException closeError = assertThrows(ZlinkException.class,
                dealer::close);
            assertEquals(ERRNO_EFSM, closeError.getInternalErrno());
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

    private static ExecutorService daemonExecutor(String name) {
        return Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, name);
            thread.setDaemon(true);
            return thread;
        });
    }

}
