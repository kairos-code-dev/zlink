package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.time.Duration;
import java.net.ServerSocket;
import java.util.EnumSet;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.MethodOrderer;
import org.junit.jupiter.api.Order;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestMethodOrder;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

@TestMethodOrder(MethodOrderer.OrderAnnotation.class)
final class SessionActorsRuntimeIntegrationTest {
    private static final LinkedBlockingQueue<String> actorRelayRequests =
        new LinkedBlockingQueue<>();

    @Test
    void bindAsyncUsesStreamActorGatewayBindingPath() {
        Zlink.version();
        try (ZLinkFrameworkRuntime runtime = startGatewayRuntime()) {
            ZLinkActor actor = runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();
            ZLinkSessionActorsRuntime sessionActors = runtime.sessionActors(
                "gateway",
                RoutingId.from("session-1"));
            ZLinkSessionActor bound = sessionActors
                .bindAsync(actor)
                .toCompletableFuture()
                .join();

            assertEquals("player-1", bound.actorId());
            assertEquals(Optional.of(bound), sessionActors.find("player-1"));
        }
    }

    @Test
    void actorContext_joinEntrySpot_joinsLocalEntrySpot() {
        Zlink.version();
        try (ZLinkFrameworkRuntime runtime = startGatewayRuntime()) {
            ZLinkActor actor = runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();

            systems.zlink.framework.actors.ZLinkActorRef joined = actor.context()
                .joinEntrySpot(RoutingId.from("play-node"))
                .timeout(Duration.ofSeconds(2))
                .submitAsync()
                .toCompletableFuture()
                .join();

            assertEquals("player-1", joined.actorId());
            assertEquals(RoutingId.from("play-node"), joined.nodeRid());
        }
    }

    @Test
    void actorContext_joinEntrySpot_joinsBoundLocalEntrySpot() {
        Zlink.version();
        try (ZLinkFrameworkRuntime runtime = startBoundGatewayRuntime()) {
            ZLinkActor actor = runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();

            systems.zlink.framework.actors.ZLinkActorRef joined = actor.context()
                .joinEntrySpot(RoutingId.from("play-node"))
                .timeout(Duration.ofSeconds(2))
                .submitAsync()
                .toCompletableFuture()
                .join();

            assertEquals("player-1", joined.actorId());
            assertEquals(RoutingId.from("play-node"), joined.nodeRid());
        }
    }

    @Test
    void channelRequestHandler_canCreateActorAndJoinEntrySpot() {
        Zlink.version();
        try (ZLinkFrameworkRuntime runtime = startChannelJoinRuntime()) {
            String actorId = runtime.client()
                .requestToChannel("play", "player-1")
                .packetName("Ensure")
                .timeout(Duration.ofSeconds(2))
                .submitAsync(String.class)
                .toCompletableFuture()
                .join();

            assertEquals("player-1", actorId);
        }
    }

    @Test
    void sessionGateway_bindsRemoteActorRefThroughDiscoveredSpotMesh() throws Exception {
        Zlink.version();
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String playRouter = tcpEndpoint();
        String playPub = tcpEndpoint();
        String sessionRouter = tcpEndpoint();
        String sessionPub = tcpEndpoint();
        String streamEndpoint = tcpEndpoint();
        ZLinkEmbeddedRegistryOptions registryOptions = new ZLinkEmbeddedRegistryOptions();
        registryOptions.setPubEndpoint(registryPub);
        registryOptions.setRouterEndpoint(registryRouter);

        try (ZLinkRegistryRuntime ignoredRegistry = new ZLinkRegistryRuntime(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
            ZLinkFrameworkRuntime play = startDiscoveredPlayRuntime(
                 registryRouter,
                 playRouter,
                 playPub);
             ZLinkFrameworkRuntime session = startDiscoveredSessionRuntime(
                 registryRouter,
                 sessionRouter,
                 sessionPub,
                 streamEndpoint)) {
            ZLinkActor actor = play.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();
            systems.zlink.framework.actors.ZLinkActorRef joined = actor.context()
                .joinEntrySpot(RoutingId.from("play-node"))
                .timeout(Duration.ofSeconds(2))
                .submitAsync()
                .toCompletableFuture()
                .join();

            ZLinkSessionActor bound = session.sessionActors(
                    "gateway",
                    RoutingId.from("session-1"))
                .bindAsync(joined)
                .toCompletableFuture()
                .join();

            assertEquals("player-1", bound.actorId());
        }
    }

    @Test
    @Order(10)
    void sessionGateway_relaysRemoteActorRequestThroughDiscoveredSpotMesh() throws Exception {
        actorRelayRequests.clear();
        Zlink.version();
        String actorId = uniqueActorId("raw-player");
        RoutingId sessionRid = RoutingId.from(uniqueActorId("raw-session"));
        RoutingId playNodeRid = RoutingId.from(uniqueActorId("raw-play-node"));
        RoutingId sessionNodeRid = RoutingId.from(uniqueActorId("raw-session-node"));
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String playRouter = tcpEndpoint();
        String playPub = tcpEndpoint();
        String sessionRouter = tcpEndpoint();
        String sessionPub = tcpEndpoint();
        String streamEndpoint = tcpEndpoint();
        ZLinkEmbeddedRegistryOptions registryOptions = new ZLinkEmbeddedRegistryOptions();
        registryOptions.setPubEndpoint(registryPub);
        registryOptions.setRouterEndpoint(registryRouter);

        try (ZLinkRegistryRuntime ignoredRegistry = new ZLinkRegistryRuntime(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime play = startDiscoveredPlayRuntime(
                 registryRouter,
                 playRouter,
                 playPub,
                 false,
                 playNodeRid);
             ZLinkFrameworkRuntime session = startDiscoveredSessionRuntime(
                 registryRouter,
                 sessionRouter,
                 sessionPub,
                 streamEndpoint,
                 sessionNodeRid)) {
            ZLinkActor actor = play.actorManager()
                .createAsync(actorId, "player")
                .toCompletableFuture()
                .join();
            systems.zlink.framework.actors.ZLinkActorRef joined = actor.context()
                .joinEntrySpot(playNodeRid)
                .timeout(Duration.ofSeconds(2))
                .submitAsync()
                .toCompletableFuture()
                .join();

            ZLinkSessionActor bound = session.sessionActors(
                    "gateway",
                    sessionRid)
                .bindAsync(joined)
                .toCompletableFuture()
                .join();

            assertEquals(
                actorId + ":raw-hello",
                relayUntilActorReceived(
                    bound,
                    new ZLinkStreamHeader(
                        "ActorEcho",
                        java.util.Map.of(),
                        Optional.of(1L)),
                    "raw-hello".getBytes(java.nio.charset.StandardCharsets.UTF_8),
                    actorId + ":raw-hello",
                    10,
                    TimeUnit.SECONDS));
        }
    }

    @Test
    @Order(20)
    void sessionGateway_relaysJsonActorRequestWithDefaultPacketNameThroughDiscoveredSpotMesh()
        throws Exception {
        actorRelayRequests.clear();
        Zlink.version();
        String actorId = uniqueActorId("json-player");
        RoutingId sessionRid = RoutingId.from(uniqueActorId("json-session"));
        RoutingId playNodeRid = RoutingId.from(uniqueActorId("json-play-node"));
        RoutingId sessionNodeRid = RoutingId.from(uniqueActorId("json-session-node"));
        String registryPub = tcpEndpoint();
        String registryRouter = tcpEndpoint();
        String playRouter = tcpEndpoint();
        String playPub = tcpEndpoint();
        String sessionRouter = tcpEndpoint();
        String sessionPub = tcpEndpoint();
        String streamEndpoint = tcpEndpoint();
        ZLinkEmbeddedRegistryOptions registryOptions = new ZLinkEmbeddedRegistryOptions();
        registryOptions.setPubEndpoint(registryPub);
        registryOptions.setRouterEndpoint(registryRouter);

        try (ZLinkRegistryRuntime ignoredRegistry = new ZLinkRegistryRuntime(
                 registryOptions,
                 new ZLinkJavaBackendAdapterFactory(),
                 new ZLinkBackendAdapterOptions(Duration.ofSeconds(1)));
             ZLinkFrameworkRuntime play = startDiscoveredPlayRuntime(
                 registryRouter,
                 playRouter,
                 playPub,
                 true,
                 playNodeRid);
             ZLinkFrameworkRuntime session = startDiscoveredSessionRuntime(
                 registryRouter,
                 sessionRouter,
                 sessionPub,
                 streamEndpoint,
                 sessionNodeRid)) {
            ZLinkActor actor = play.actorManager()
                .createAsync(actorId, "player")
                .toCompletableFuture()
                .join();
            systems.zlink.framework.actors.ZLinkActorRef joined = actor.context()
                .joinEntrySpot(playNodeRid)
                .timeout(Duration.ofSeconds(2))
                .submitAsync()
                .toCompletableFuture()
                .join();

            ZLinkSessionActor bound = session.sessionActors(
                    "gateway",
                    sessionRid)
                .bindAsync(joined)
                .toCompletableFuture()
                .join();

            assertEquals(
                actorId + ":json-hello",
                relayUntilActorReceived(
                    bound,
                    new ZLinkStreamHeader(
                        ZLinkStreamMessageKind.REQUEST,
                        ZLinkStreamCodec.JSON,
                        EnumSet.of(ZLinkStreamHeaderFlag.HAS_REQUEST_SEQUENCE),
                        Optional.of(1L),
                        "JsonRelayReq",
                        java.util.Map.of()),
                    "{\"value\":\"json-hello\"}".getBytes(java.nio.charset.StandardCharsets.UTF_8),
                    actorId + ":json-hello",
                    20,
                    TimeUnit.SECONDS));
        }
    }

    @Test
    void sessionAndPlayServers_relaySucceeds() {
        Zlink.version();
        try (ZLinkFrameworkRuntime runtime = startGatewayRuntime()) {
            ZLinkActor actor = runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();
            systems.zlink.framework.actors.ZLinkActorRef actorRef = actor.context()
                .joinEntrySpot(RoutingId.from("play-node"))
                .timeout(Duration.ofSeconds(2))
                .submitAsync()
                .toCompletableFuture()
                .join();
            ZLinkSessionActorsRuntime sessionActors = runtime.sessionActors(
                "gateway",
                RoutingId.from("session-1"));
            ZLinkSessionActor bound = sessionActors
                .bindAsync(actorRef)
                .toCompletableFuture()
                .join();

            assertEquals("player-1", bound.actorId());
            assertEquals(Optional.of(bound), sessionActors.find("player-1"));
        }
    }

    @Test
    void playActorPush_withoutLiveClientStreamFailsNativeSend() {
        Zlink.version();
        try (ZLinkFrameworkRuntime runtime = startGatewayRuntime()) {
            ZLinkActor actor = runtime.actorManager()
                .createAsync("player-1", "player")
                .toCompletableFuture()
                .join();
            runtime.sessionActors("gateway", RoutingId.from("session-1"))
                .bindAsync(actor)
                .toCompletableFuture()
                .join();

            assertThrows(ZlinkSubmitException.class, () -> actor.context()
                    .boundSession()
                    .send("push")
                    .packetName("Push")
                    .submitAsync()
                    .toCompletableFuture()
                    .join());
        }
    }

    private static ZLinkFrameworkRuntime startGatewayRuntime() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router ->
                    router.setRoutingId(RoutingId.from("play-node")));
                node.addSpotFactory(GameSpot.class);
                node.addEntrySpot(GameEntrySpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway-bind-" + System.nanoTime());
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class);
        });

        return ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory());
    }

    private static ZLinkFrameworkRuntime startBoundGatewayRuntime() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        String suffix = Long.toString(System.nanoTime());
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router -> {
                    router.setRouterBind("inproc://play-router-" + suffix);
                    router.setRoutingId(RoutingId.from("play-node"));
                });
                node.enablePubSub(pubSub ->
                    pubSub.setPubBind("inproc://play-pub-" + suffix));
                node.addSpotFactory(GameSpot.class);
                node.addEntrySpot(GameEntrySpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);

        return ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory());
    }

    private static ZLinkFrameworkRuntime startChannelJoinRuntime() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        String endpoint = "inproc://play-channel-" + System.nanoTime();
        options.addHandlersFromPackageOf(SessionActorsRuntimeIntegrationTest.class);
        options.addClientServerChannel("play", channel -> {
            channel.enableServer(server -> server.bind(endpoint));
            channel.enableClient(client ->
                client.useManualConnections(endpoints -> endpoints.connect(endpoint)));
            channel.addHandlerGroup("play-channel");
        });
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router ->
                    router.setRoutingId(RoutingId.from("play-node")));
                node.addEntrySpot(GameEntrySpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);

        return ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory());
    }

    private static ZLinkFrameworkRuntime startDiscoveredPlayRuntime(
        String registryRouter,
        String playRouter,
        String playPub) {
        return startDiscoveredPlayRuntime(registryRouter, playRouter, playPub, false);
    }

    private static ZLinkFrameworkRuntime startDiscoveredPlayRuntime(
        String registryRouter,
        String playRouter,
        String playPub,
        boolean json) {
        return startDiscoveredPlayRuntime(
            registryRouter,
            playRouter,
            playPub,
            json,
            RoutingId.from("play-node"));
    }

    private static ZLinkFrameworkRuntime startDiscoveredPlayRuntime(
        String registryRouter,
        String playRouter,
        String playPub,
        boolean json,
        RoutingId playNodeRid) {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SessionActorsRuntimeIntegrationTest.class);
        if (json) {
            options.codecs().addJson();
        }
        options.useDiscovery(discovery -> discovery.add(registryRouter));
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router -> {
                    router.setRouterBind(playRouter);
                    router.setRoutingId(playNodeRid);
                });
                node.enablePubSub(pubSub -> pubSub.setPubBind(playPub));
                node.addEntrySpot(GameEntrySpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);

        return ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory());
    }

    private static ZLinkFrameworkRuntime startDiscoveredSessionRuntime(
        String registryRouter,
        String sessionRouter,
        String sessionPub,
        String streamEndpoint) {
        return startDiscoveredSessionRuntime(
            registryRouter,
            sessionRouter,
            sessionPub,
            streamEndpoint,
            RoutingId.from("session-node"));
    }

    private static ZLinkFrameworkRuntime startDiscoveredSessionRuntime(
        String registryRouter,
        String sessionRouter,
        String sessionPub,
        String streamEndpoint,
        RoutingId sessionNodeRid) {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useDiscovery(discovery -> discovery.add(registryRouter));
        options.addSpotMesh("game", mesh ->
            mesh.addNode("session", node -> {
                node.enableRouter(router -> {
                    router.setRouterBind(sessionRouter);
                    router.setRoutingId(sessionNodeRid);
                });
                node.enablePubSub(pubSub -> pubSub.setPubBind(sessionPub));
            }));
        options.addStreamNode("gateway", stream -> {
            stream.bind(streamEndpoint);
            stream.attachActorGateway("session");
            stream.registerSession(GameSession.class);
        });

        return ZLinkFrameworkRuntime.start(options, new ZLinkJavaBackendAdapterFactory());
    }

    private static String tcpEndpoint() throws Exception {
        try (ServerSocket server = new ServerSocket(0)) {
            return "tcp://127.0.0.1:" + server.getLocalPort();
        }
    }

    public static final class PlayerActor implements ZLinkActor {
        private final String actorId;
        private final ZLinkActorContext context;

        PlayerActor(String actorId, ZLinkActorContext context) {
            this.actorId = actorId;
            this.context = context;
        }

        @Override
        public String actorId() {
            return actorId;
        }

        @Override
        public ZLinkActorContext context() {
            return context;
        }
    }

    public static final class PlayerActorFactory implements ZLinkActorFactory {
        @Override
        public CompletionStage<ZLinkActor> createAsync(
            String actorId,
            ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new PlayerActor(actorId, context));
        }
    }

    public static final class GameSpot implements ZLinkSpot {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }
    }

    public static final class GameEntrySpot implements systems.zlink.framework.spots.ZLinkEntrySpot {
        private final systems.zlink.framework.spots.ZLinkEntrySpotContext context;

        public GameEntrySpot(systems.zlink.framework.spots.ZLinkEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public systems.zlink.framework.spots.ZLinkEntrySpotContext context() {
            return context;
        }
    }

    public static final class ActorEchoHandler {
        @ZLinkSpotActorRequest(packetName = "ActorEcho")
        public CompletionStage<String> handleAsync(PlayerActor actor, String request) {
            actorRelayRequests.offer(actor.actorId() + ":" + request);
            return CompletableFuture.completedFuture(request);
        }
    }

    public record JsonRelayReq(String value) {
    }

    public record JsonRelayRes(String value) {
    }

    public static final class DefaultJsonActorHandler {
        @ZLinkSpotActorRequest
        public CompletionStage<JsonRelayRes> handleAsync(PlayerActor actor, JsonRelayReq request) {
            actorRelayRequests.offer(actor.actorId() + ":" + request.value());
            return CompletableFuture.completedFuture(new JsonRelayRes(request.value()));
        }
    }

    private static String awaitActorRelay(
        String expected,
        long timeout,
        TimeUnit unit) throws InterruptedException, java.util.concurrent.TimeoutException {
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        while (true) {
            long remaining = deadline - System.nanoTime();
            if (remaining <= 0) {
                throw new java.util.concurrent.TimeoutException();
            }
            String received = actorRelayRequests.poll(remaining, TimeUnit.NANOSECONDS);
            if (expected.equals(received)) {
                return received;
            }
        }
    }

    private static String relayUntilActorReceived(
        ZLinkSessionActor bound,
        ZLinkStreamHeader header,
        byte[] payloadBytes,
        String expected,
        long timeout,
        TimeUnit unit) throws Exception {
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        while (true) {
            try (Message payload = Message.from(payloadBytes)) {
                bound.relayAsync(header, payload).toCompletableFuture().join();
            }
            long remaining = deadline - System.nanoTime();
            if (remaining <= 0) {
                throw new java.util.concurrent.TimeoutException();
            }
            String received = actorRelayRequests.poll(
                Math.min(remaining, TimeUnit.MILLISECONDS.toNanos(100)),
                TimeUnit.NANOSECONDS);
            if (expected.equals(received)) {
                return received;
            }
        }
    }

    private static String uniqueActorId(String prefix) {
        return prefix + "-" + Long.toUnsignedString(System.nanoTime(), 36);
    }

    @systems.zlink.framework.handlers.ZLinkHandlerGroup("play-channel")
    public static final class EnsureActorHandler {
        private final ZLinkActorManager actors;

        public EnsureActorHandler(ZLinkActorManager actors) {
            this.actors = actors;
        }

        @ZLinkRequest(packetName = "Ensure")
        public CompletionStage<String> handleAsync(String actorId) {
            return actors.getOrCreateAsync(actorId, "player")
                .thenCompose(actor -> actor.context()
                    .joinEntrySpot(RoutingId.from("play-node"))
                    .timeout(Duration.ofSeconds(2))
                    .submitAsync())
                .thenApply(joined -> joined.actorId());
        }
    }

    public static final class GameSession implements ZLinkSession {
        @Override
        public ZLinkSessionContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onConnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }
    }
}
