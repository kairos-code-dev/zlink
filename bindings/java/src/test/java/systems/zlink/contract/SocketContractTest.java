package systems.zlink.contract;

import systems.zlink.Context;
import systems.zlink.CommonSocketOptions;
import systems.zlink.DealerSocket;
import systems.zlink.Message;
import systems.zlink.MonitorSocket;
import systems.zlink.PairSocket;
import systems.zlink.PollEventFlag;
import systems.zlink.Poller;
import systems.zlink.PubSocket;
import systems.zlink.PubSocketOptions;
import systems.zlink.RecvException;
import systems.zlink.RecvFlags;
import systems.zlink.Received;
import systems.zlink.RequestResult;
import systems.zlink.RouterSocket;
import systems.zlink.RoutingId;
import systems.zlink.SendFlags;
import systems.zlink.SubmitException;
import systems.zlink.SubmitResult;
import systems.zlink.service.registry.MemberPeerEntry;
import systems.zlink.service.registry.AutoConnectType;
import systems.zlink.StreamSocket;
import systems.zlink.SubSocket;
import systems.zlink.SubSocketOptions;
import systems.zlink.TestSupport;
import systems.zlink.Timer;
import systems.zlink.TopicMessage;
import systems.zlink.XPubSocket;
import systems.zlink.XSubSocket;
import systems.zlink.Zlink;
import systems.zlink.ZlinkException;
import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.spot.SpotNode;
import systems.zlink.service.registry.Registry;
import systems.zlink.service.spot.ActorRef;
import systems.zlink.service.spot.ActorJoinCallbackSubmitOp;
import systems.zlink.service.spot.ActorJoinOp;
import systems.zlink.service.spot.ActorJoinSubmitOp;
import systems.zlink.service.spot.ActorRoute;
import systems.zlink.service.spot.ActorBindOp;
import systems.zlink.service.spot.ActorUnbindOp;
import systems.zlink.service.spot.ReplyOp;
import systems.zlink.service.spot.RequestOp;
import systems.zlink.service.spot.SendOp;
import systems.zlink.service.spot.Spot;
import systems.zlink.service.spot.SpotNodeActorEntry;
import java.lang.foreign.MemorySegment;
import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
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
                client.send().message(outbound).submit();
            }

            try (systems.zlink.Received received = new systems.zlink.Received()) {


                server.recv(received, systems.zlink.RecvFlags.NONE);
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
                try (systems.zlink.Received received = new systems.zlink.Received()) {

                    routerSocket.recv(received, systems.zlink.RecvFlags.NONE);
                    assertArrayEquals("ping".getBytes(StandardCharsets.UTF_8),
                        received.singlePartOrThrow().toByteArray());
                    assertTrue(received.routingId().isPresent());
                    assertTrue(received.requestSeq().isPresent());
                    assertTrue(received.requestSeq().orElseThrow() != 0L);
                    received.reply()
                        .message(Message.copyOfUtf8("pong"))
                        .submit();
                }
            }, serverExecutor);

            try (Message request = Message.copyOfUtf8("ping")) {
                List<Message> reply = dealerSocket.request()
                    .message(request)
                    .timeout(Duration.ofSeconds(2))
                    .submitAsync()
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
    public void requestReplyWrapperPreservesDataReceiveSurface() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             RouterSocket routerSocket = new RouterSocket(ctx);
             DealerSocket dealerSocket = new DealerSocket(ctx)) {
            String endpoint = TestSupport.inprocEndpoint("request-reply-data");
            routerSocket.bind(endpoint);
            dealerSocket.connect(endpoint);

            dealerSocket.send()
                .message(Message.copyOfUtf8("plain-data"))
                .submit();

            try (systems.zlink.Received received = new systems.zlink.Received()) {


                routerSocket.recv(received, systems.zlink.RecvFlags.NONE);
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
                try (systems.zlink.Received received = new systems.zlink.Received()) {

                    routerSocket.recv(received, systems.zlink.RecvFlags.NONE);
                    received.reply()
                        .message(Message.copyOfUtf8("pong-callback"))
                        .submit();
                }
            }, serverExecutor);

            CountDownLatch done = new CountDownLatch(1);
            AtomicReference<List<Message>> replyRef = new AtomicReference<>();
            AtomicReference<RequestResult> resultRef = new AtomicReference<>();
            AtomicReference<Throwable> errorRef = new AtomicReference<>();
            try (Message request = Message.copyOfUtf8("ping-callback")) {
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

        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(
               systems.zlink.MonitorEventType.CONNECTION_READY);
             var subMonitor = sub.monitorOpen(
               systems.zlink.MonitorEventType.CONNECTION_READY)) {
            String endpoint = TestSupport.inprocEndpoint("socket-pubsub-contract");
            pub.bind(endpoint);
            sub.setSubscription("socket-topic");
            sub.connect(endpoint);
            TestSupport.awaitMonitorEvent(subMonitor,
                systems.zlink.MonitorEventType.CONNECTION_READY);
            TestSupport.awaitMonitorEvent(pubMonitor,
                systems.zlink.MonitorEventType.CONNECTION_READY);

            try (Message payload = Message.copyOfUtf8("socket-payload")) {
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
             Discovery discovery = new Discovery(ctx, AutoConnectType.CLIENT_SERVER,
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
                systems.zlink.MonitorEventType[].class));
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
                    assertTrue(monitor.snapshot().sndPendingMsgs() >= 0L);
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
                RoutingId.class).getReturnType() == SendOp.class);
        assertTrue(hasPublicMethod(Spot.class, "requestToSpot",
            RoutingId.class, RoutingId.class)
            && Spot.class.getMethod("requestToSpot", RoutingId.class,
                RoutingId.class).getReturnType() == RequestOp.class);
        assertTrue(hasPublicMethod(Spot.class, "replyToSpot",
            RoutingId.class, RoutingId.class, long.class)
            && Spot.class.getMethod("replyToSpot", RoutingId.class,
                RoutingId.class, long.class).getReturnType() == ReplyOp.class);
        assertTrue(hasPublicMethod(Spot.class, "replyToRouter",
            RoutingId.class, long.class)
            && Spot.class.getMethod("replyToRouter", RoutingId.class,
                long.class).getReturnType() == ReplyOp.class);
        assertFalse(hasPublicMethod(Spot.class, "sendToSpot",
            RoutingId.class, RoutingId.class, Message.class));
        assertFalse(hasPublicMethod(Spot.class, "replyToSpot",
            RoutingId.class, RoutingId.class, long.class, Message.class));
        assertFalse(hasPublicMethod(Spot.class, "replyToRouter",
            RoutingId.class, long.class, Message.class));
        assertTrue(hasPublicMethod(Spot.class, "recvRouted"));
        assertTrue(hasPublicMethod(Spot.class, "onRoutedReceive",
            systems.zlink.SpotRoutedHandler.class));
        assertTrue(hasPublicMethod(Spot.class, "onDispatchEvent",
            systems.zlink.SpotDispatchEventHandler.class));
        assertFalse(hasPublicMethod(Spot.class, "drainChannelReply",
            systems.zlink.SpotDispatchInfo.class));
        assertFalse(hasPublicMethod(Spot.class, "drainChannelReplyFrom",
            MemorySegment.class));
        assertTrue(hasPublicMethod(Spot.class, "setRoutingId",
            RoutingId.class));
        assertTrue(hasPublicMethod(Spot.class, "routingId"));
        assertTrue(hasPublicMethod(DealerSocket.class, "setChannelName",
            String.class));
        assertTrue(hasPublicMethod(DealerSocket.class, "getChannelName"));
        assertTrue(hasPublicMethod(SpotNode.class, "setRoutingId",
            RoutingId.class));
        assertTrue(hasPublicMethod(SpotNode.class, "routingId"));
        assertTrue(hasPublicMethod(SpotNode.class, "entrySpot"));
        assertTrue(hasPublicMethod(SpotNode.class, "spotLookup",
            RoutingId.class));
        assertFalse(hasPublicMethod(SpotNode.class, "socketSnapshots"));
        assertFalse(hasPublicMethod(SpotNode.class, "socketSnapshots",
            systems.zlink.service.spot.SpotNodeSocketSnapshotFilter.class));
        assertTrue(hasPublicMethod(SpotNode.class, "internalSocketsSnapshot"));
        assertTrue(hasPublicMethod(SpotNode.class, "internalSocketsSnapshot",
            systems.zlink.service.spot.SpotNodeSocketSnapshotFilter.class));
        assertTrue(hasPublicMethod(SpotNode.class, "routerHwmProfile"));
        assertTrue(hasPublicMethod(SpotNode.class, "routerHwm", int.class));
        assertTrue(hasPublicMethod(SpotNode.class, "pubsubHwmProfile"));
        assertTrue(hasPublicMethod(SpotNode.class, "pubsubHwm", int.class));
        assertTrue(hasPublicMethod(Discovery.class, "resolveSpot",
            RoutingId.class));
        assertFalse(hasPublicMethod(Discovery.class, "bindRoute",
            int.class, byte[].class, byte[].class));
        assertFalse(hasPublicMethod(Discovery.class, "resolveRoute",
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
        assertFalse(isPublicClass("systems.zlink.SubscribeHandler"));
        assertFalse(isPublicClass("systems.zlink.SocketOption"));
        assertFalse(isPublicClass("systems.zlink.ErrorCode"));
        assertFalse(isPublicClass("systems.zlink.RequestReplyCallback"));
        assertFalse(isPublicClass("systems.zlink.SocketPollSet"));
        assertFalse(isPublicClass("systems.zlink.DisconnectReason"));
        assertFalse(isPublicClass("systems.zlink.ProtocolError"));
        assertFalse(isPublicClass("systems.zlink.InternalAccess"));
        assertFalse(isPublicClass("systems.zlink.StreamDispatchMode"));
        assertTrue(isPublicClass("systems.zlink.SubscriptionEntry"));
        assertFalse(isPublicClass("systems.zlink.ZlinkVersion"));
        assertTrue(isPublicClass("systems.zlink.SocketType"));
        assertFalse(hasPublicMethod(SubSocketOptions.class, "subscriptionAt",
            int.class));
        assertTrue(hasPublicMethod(SubSocket.class, "subscriptionAt",
            int.class));
        assertFalse(hasPublicMethod(Spot.class, "options"));
        assertTrue(hasPublicMethod(Poller.class, "add", Timer.class));
        assertTrue(hasPublicMethod(Poller.class, "add", Timer.class,
            Object.class));
        assertTrue(hasPublicMethod(Poller.class, "remove", Timer.class));
        assertTrue(hasPublicMethod(Poller.class, "add", Spot.class,
            PollEventFlag[].class));
        assertTrue(hasPublicMethod(Poller.class, "add", Spot.class,
            Object.class, PollEventFlag[].class));
        assertTrue(hasPublicMethod(Poller.class, "modify", Spot.class,
            PollEventFlag[].class));
        assertTrue(hasPublicMethod(Poller.class, "remove", Spot.class));
        assertFalse(hasPublicMethod(Poller.class, "readyTimer", int.class));
        assertTrue(hasPublicMethod(systems.zlink.MonitorSocket.class,
            "recv", RecvFlags.class));
    }

    @Test
    public void actorRouteSnapshotsUseOptionalJoinedSpotRid() {
        RoutingId nodeRid = RoutingId.fromBytes(new byte[] {1});
        ActorRef actor = new ActorRef(nodeRid, "actor", 1L);
        Optional<RoutingId> joinedSpotRid =
            Optional.of(RoutingId.fromBytes(new byte[] {2}));

        ActorRoute route = new ActorRoute(actor, true, joinedSpotRid);
        Optional<RoutingId> routeSpot = route.joinedSpotRid();
        assertEquals(joinedSpotRid, routeSpot);

        SpotNodeActorEntry entry = new SpotNodeActorEntry(actor, true,
            joinedSpotRid, true, 0, 0L);
        Optional<RoutingId> entrySpot = entry.joinedSpotRid();
        assertEquals(joinedSpotRid, entrySpot);
    }

    @Test
    public void actorJoinBuilderSurfaceMatchesJavaSpec() throws Exception {
        assertEquals(ActorJoinSubmitOp.class,
            ActorJoinOp.class.getMethod("message", Message.class)
                .getReturnType());
        assertEquals(ActorJoinSubmitOp.class,
            ActorJoinSubmitOp.class.getMethod("message", Message.class)
                .getReturnType());
        assertEquals(ActorJoinSubmitOp.class,
            ActorJoinSubmitOp.class.getMethod("timeout", Duration.class)
                .getReturnType());
        assertEquals(ActorJoinCallbackSubmitOp.class,
            ActorJoinSubmitOp.class.getMethod("flags", SendFlags.class)
                .getReturnType());
        assertEquals(ActorJoinCallbackSubmitOp.class,
            ActorJoinCallbackSubmitOp.class.getMethod("message", Message.class)
                .getReturnType());
        assertEquals(ActorJoinCallbackSubmitOp.class,
            ActorJoinCallbackSubmitOp.class.getMethod("timeout", Duration.class)
                .getReturnType());
        assertEquals(ActorJoinCallbackSubmitOp.class,
            ActorJoinCallbackSubmitOp.class.getMethod("flags", SendFlags.class)
                .getReturnType());
        assertFalse(hasPublicMethod(ActorJoinCallbackSubmitOp.class,
            "submitAsync"));
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
                future = dealerSocket.request()
                    .message(request)
                    .timeout(Duration.ofMillis(50))
                    .submitAsync();
            }

            try (systems.zlink.Received received = new systems.zlink.Received()) {


                routerSocket.recv(received, systems.zlink.RecvFlags.NONE);
                SubmitException submitException = assertThrows(
                    SubmitException.class,
                    () -> received.reply()
                        .message(Message.copyOfUtf8("pong"))
                        .flags(SendFlags.DONT_WAIT)
                        .submit());
                assertEquals(SubmitResult.NOT_SUPPORTED,
                    submitException.getResult());
            }

            ExecutionException completion = assertThrows(ExecutionException.class,
                () -> future.get(1, TimeUnit.SECONDS));
            assertTrue(completion.getCause() instanceof systems.zlink.RequestException);
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
            systems.zlink.StreamPacketHandler.class));
        assertFalse(hasPublicMethod(StreamSocket.class, "onPacketNative"));
        assertFalse(hasPublicMethod(StreamSocket.class, "onFramedPacket"));
        assertFalse(hasPublicMethod(StreamSocket.class, "onFramedPacketNative"));
        assertFalse(hasPublicMethod(StreamSocket.class, "sendCopied",
            int.class, java.lang.foreign.MemorySegment.class, int.class,
            SendFlags.class));
        assertFalse(hasPublicMethod(StreamSocket.class, "send",
            int.class, java.lang.foreign.MemorySegment.class, int.class,
            SendFlags.class));
        assertFalse(hasPublicMethod(StreamSocket.class, "attachStreamRaw"));
        assertFalse(hasPublicMethod(StreamSocket.class, "connect"));
        assertFalse(hasPublicMethod(StreamSocket.class, "attachDiscovery"));
        assertEquals(SendOp.class, StreamSocket.class
            .getMethod("send", RoutingId.class).getReturnType());
        assertEquals(ActorBindOp.class, StreamSocket.class
            .getMethod("bindActor", RoutingId.class, ActorRef.class)
            .getReturnType());
        assertEquals(ActorUnbindOp.class, StreamSocket.class
            .getMethod("unbindActor", RoutingId.class, String.class)
            .getReturnType());
        assertEquals(SendOp.class, StreamSocket.class
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

        try (Context ctx = new Context();
             Discovery discovery = new Discovery(ctx, AutoConnectType.CLIENT_SERVER, "svc");
             SpotNode node = new SpotNode(ctx)) {
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
            assertFalse(hasPublicMethod(systems.zlink.service.spot.SpotNode.class,
                "monitorOpen", int.class));
            assertTrue(hasPublicMethod(MonitorSocket.class, "recv",
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
        }
    }

    @Test
    public void receivedAndMemberPeerSurfaceMatchesJavaSpec() {
        assertFalse(Iterable.class.isAssignableFrom(Received.class));
        assertFalse(hasPublicMethod(Received.class, "iterator"));
        assertFalse(hasPublicMethod(Received.class, "routingIdOrNull"));
        assertFalse(hasPublicMethod(Received.class, "routingIdOrThrow"));
        assertFalse(hasPublicMethod(Received.class, "spotRidOrNull"));
        assertFalse(hasPublicMethod(systems.zlink.MonitorSnapshot.class,
            "fromNative", java.lang.foreign.MemorySegment.class));
        assertFalse(hasPublicMethod(
            systems.zlink.service.registry.MemberPeerEntry.class,
            "fromNative", java.lang.foreign.MemorySegment.class));
        assertEquals(7, MemberPeerEntry.class.getRecordComponents().length);
        assertEquals("weight",
            MemberPeerEntry.class.getRecordComponents()[6].getName());
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

            try (systems.zlink.Received probe = new systems.zlink.Received()) {
                assertFalse(server.recv(probe, RecvFlags.DONT_WAIT));
            }
            try (Message outbound = Message.copyOfUtf8("pair-try")) {
                assertTrue(client.send().message(outbound).flags(SendFlags.DONT_WAIT).submit());
            }
            try (systems.zlink.Received received = new systems.zlink.Received()) {
                assertTrue(server.recv(received, RecvFlags.DONT_WAIT));
                assertEquals(List.of("pair-try"),
                    List.of(received.singlePartOrThrow().toUtf8String()));
            }
        }
    }

    @Test
    public void attachDiscoveryGatesManualPeerApisAndSocketClose() {
        TestSupport.assumeNative();

        try (Context ctx = new Context();
             Discovery discovery = new Discovery(ctx, AutoConnectType.CLIENT_SERVER,
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
