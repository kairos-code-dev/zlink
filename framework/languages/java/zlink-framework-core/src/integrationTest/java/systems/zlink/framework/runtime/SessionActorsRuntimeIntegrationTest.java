package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.net.ServerSocket;
import java.nio.charset.StandardCharsets;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorSend;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.registry.ZLinkEmbeddedRegistryOptions;
import systems.zlink.framework.runtime.actors.ZLinkSessionActorsRuntime;
import systems.zlink.framework.runtime.actors.ZLinkActorRuntime;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.backend.ZLinkBackendAdapterOptions;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.runtime.registry.ZLinkRegistryRuntime;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;

final class SessionActorsRuntimeIntegrationTest {
    static final LinkedBlockingQueue<String> actorRelayRequests =
        new LinkedBlockingQueue<>();

    @Test
    void bindUsesStreamActorGatewayBindingPath() {
        Zlink.version();
        try (ZLinkFrameworkRuntime runtime = startGatewayRuntime()) {
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            ZLinkSessionActorsRuntime sessionActors = runtime.sessionActors(
                "gateway",
                RoutingId.from("session-1"));
            ZLinkSessionActor bound = sessionActors
                .bind(actor)
                .toCompletableFuture()
                .join();

            assertEquals("player-1", bound.actorId());
            assertEquals(Optional.of(bound), sessionActors.find("player-1"));
        }
    }

    @Test
    void bindCanRelayLocalManagedActorWithoutActorGatewayAttach() throws Exception {
        actorRelayRequests.clear();
        Zlink.version();
        try (ZLinkFrameworkRuntime runtime = startLocalManagedStreamRuntime()) {
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            ZLinkSessionActor bound = runtime.sessionActors(
                    "local",
                    RoutingId.from("session-1"))
                .bind(actor)
                .toCompletableFuture()
                .join();

            relayWithHeader(bound, "ActorNotify", ZLinkMessage.of("hello"));

            assertEquals(
                "player-1:hello",
                awaitActorRelay("player-1:hello", 2, TimeUnit.SECONDS));
        }
    }

    @Test
    void actorContext_joinEntrySpot_joinsLocalEntrySpot() {
        Zlink.version();
        try (ZLinkFrameworkRuntime runtime = startGatewayRuntime()) {
            ZLinkActor actor = managedActor(runtime, "player-1", "player");

            var joined = actor.context()
                .joinEntrySpot(RoutingId.from("play-node"))
                .timeout(Duration.ofSeconds(2))
                .submit()
                .toCompletableFuture()
                .join();

            assertEquals("player-1", joined.actor().actorId());
            assertEquals(RoutingId.from("play-node"), joined.actor().nodeRid());
        }
    }

    @Test
    void actorContext_joinEntrySpot_joinsBoundLocalEntrySpot() {
        Zlink.version();
        try (ZLinkFrameworkRuntime runtime = startBoundGatewayRuntime()) {
            ZLinkActor actor = managedActor(runtime, "player-1", "player");

            var joined = actor.context()
                .joinEntrySpot(RoutingId.from("play-node"))
                .timeout(Duration.ofSeconds(2))
                .submit()
                .toCompletableFuture()
                .join();

            assertEquals("player-1", joined.actor().actorId());
            assertEquals(RoutingId.from("play-node"), joined.actor().nodeRid());
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
                .submit(String.class)
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

        try (ZLinkRegistryRuntime ignoredRegistry = RuntimeTestSupport.startRegistry(
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
            ZLinkActor actor = managedActor(play, "player-1", "player");
            var joined = actor.context()
                .joinEntrySpot(RoutingId.from("play-node"))
                .timeout(Duration.ofSeconds(2))
                .submit()
                .toCompletableFuture()
                .join();

            ZLinkSessionActor bound = session.sessionActors(
                    "gateway",
                    RoutingId.from("session-1"))
                .bind(joined.actor())
                .toCompletableFuture()
                .join();

            assertEquals("player-1", bound.actorId());
        }
    }

    @Test
    void sessionAndPlayServers_relaySucceeds() {
        Zlink.version();
        try (ZLinkFrameworkRuntime runtime = startGatewayRuntime()) {
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            ZLinkActorRef actorRef = actor.context()
                .joinEntrySpot(RoutingId.from("play-node"))
                .timeout(Duration.ofSeconds(2))
                .submit()
                .toCompletableFuture()
                .join()
                .actor();
            ZLinkSessionActorsRuntime sessionActors = runtime.sessionActors(
                "gateway",
                RoutingId.from("session-1"));
            ZLinkSessionActor bound = sessionActors
                .bind(actorRef)
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
            ZLinkActor actor = managedActor(runtime, "player-1", "player");
            runtime.sessionActors("gateway", RoutingId.from("session-1"))
                .bind(actor)
                .toCompletableFuture()
                .join();

            CompletionException failure = assertThrows(CompletionException.class, () -> actor.context()
                    .boundSession()
                    .send("push")
                    .packetName("Push")
                    .submit()
                    .toCompletableFuture()
                    .join());
            assertTrue(failure.getCause() instanceof ZlinkSubmitException);
        }
    }

    private static ZLinkActor managedActor(
        ZLinkFrameworkRuntime runtime,
        String actorId,
        String actorType) {
        return ((ZLinkActorRuntime) runtime.actorManager())
            .getOrCreateManagedActor(actorId, actorType)
            .toCompletableFuture()
            .join();
    }

    private static ZLinkFrameworkRuntime startGatewayRuntime() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.setRoutingId(RoutingId.from("play-node"));
                node.addSpotFactory(GameSpot.class);
                node.addEntrySpot(GameEntrySpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway-bind-" + System.nanoTime());
            stream.registerSession(GameSession.class); };

        return RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory());
    }

    private static ZLinkFrameworkRuntime startLocalManagedStreamRuntime() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SessionActorsRuntimeIntegrationTest.class);
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.setRoutingId(RoutingId.from("play-node"));
                node.addEntrySpot(GameEntrySpot.class);
                node.addSpotFactory(GameSpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };
        { var stream = options.addStreamNode("local"); stream.bind("inproc://local-managed-bind-" + System.nanoTime());
            stream.registerSession(GameSession.class); };

        return RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory());
    }

    private static ZLinkFrameworkRuntime startBoundGatewayRuntime() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        String suffix = Long.toString(System.nanoTime());
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter("inproc://play-router-" + suffix)
                    .setRoutingId(RoutingId.from("play-node"));
                node.enablePubSub("inproc://play-pub-" + suffix);
                node.addSpotFactory(GameSpot.class);
                node.addEntrySpot(GameEntrySpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };

        return RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory());
    }

    private static ZLinkFrameworkRuntime startChannelJoinRuntime() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        String endpoint = "inproc://play-channel-" + System.nanoTime();
        options.addHandlersFromPackageOf(SessionActorsRuntimeIntegrationTest.class);
        { var channel = options.addClientServerChannel("play").enableServer(endpoint);
            channel.enableClient(endpoint);
            channel.addHandlerGroup("play-channel"); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.setRoutingId(RoutingId.from("play-node"));
                node.addEntrySpot(GameEntrySpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };

        return RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory());
    }

    private static ZLinkFrameworkRuntime startDiscoveredPlayRuntime(
        String registryRouter,
        String playRouter,
        String playPub) {
        return startDiscoveredPlayRuntime(
            registryRouter,
            playRouter,
            playPub,
            RoutingId.from("play-node"));
    }

    static ZLinkFrameworkRuntime startDiscoveredPlayRuntime(
        String registryRouter,
        String playRouter,
        String playPub,
        RoutingId playNodeRid) {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(SessionActorsRuntimeIntegrationTest.class);
        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter(playRouter)
                    .setRoutingId(playNodeRid);
                node.enablePubSub(playPub);
                node.addEntrySpot(GameEntrySpot.class); node.addActorFactory("player", PlayerActorFactory.class); }; };

        return RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory());
    }

    static ZLinkFrameworkRuntime startDiscoveredSessionRuntime(
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

    static ZLinkFrameworkRuntime startDiscoveredSessionRuntime(
        String registryRouter,
        String sessionRouter,
        String sessionPub,
        String streamEndpoint,
        RoutingId sessionNodeRid) {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var discovery = options.useDiscovery(); discovery.addRegistryEndpoint(registryRouter); };
        { var mesh = options.addSpotMesh("game"); { var node = mesh; node.enableRouter(sessionRouter)
                    .setRoutingId(sessionNodeRid);
                node.enablePubSub(sessionPub); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind(streamEndpoint);
            stream.registerSession(GameSession.class); };

        return RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory());
    }

    static String tcpEndpoint() throws Exception {
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
        public ZLinkActor create(
            String actorId,
            ZLinkActorContext context) {
            return new PlayerActor(actorId, context);
        }
    }

    public static final class GameSpot implements ZLinkSpot<ZLinkActor> {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }
    }

    public static final class GameEntrySpot implements ZLinkEntrySpot<ZLinkActor> {
        private final ZLinkEntrySpotContext context;

        public GameEntrySpot(ZLinkEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
        }
    }

    public static final class ActorEchoHandler {
        @ZLinkSpotActorRequest(packetName = "ActorEcho")
        public String handle(PlayerActor actor, String request) {
            actorRelayRequests.offer(actor.actorId() + ":" + request);
            return request;
        }
    }

    public static final class ActorNotifyHandler {
        @ZLinkSpotActorSend(packetName = "ActorNotify")
        public void handle(PlayerActor actor, String request) {
            actorRelayRequests.offer(actor.actorId() + ":" + request);
                    }
    }

    public record JsonRelayReq(String value) {
    }

    public record JsonRelayRes(String value) {
    }

    public static final class DefaultJsonActorHandler {
        @ZLinkSpotActorRequest
        public JsonRelayRes handle(PlayerActor actor, JsonRelayReq request) {
            actorRelayRequests.offer(actor.actorId() + ":" + request.value());
            return new JsonRelayRes(request.value());
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

    static String relayUntilActorReceived(
        ZLinkSessionActor bound,
        byte[] payloadBytes,
        String expected,
        long timeout,
        TimeUnit unit) throws Exception {
        long deadline = System.nanoTime() + unit.toNanos(timeout);
        while (true) {
            relayWithHeader(
                bound,
                "ActorNotify",
                ZLinkMessage.of(new String(payloadBytes, StandardCharsets.UTF_8)));
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

    static String uniqueActorId(String prefix) {
        return prefix + "-" + Long.toUnsignedString(System.nanoTime(), 36);
    }

    static void relayWithHeader(
        ZLinkSessionActor actor,
        String packetName,
        ZLinkMessage payload) {
        ZLinkSessionActorsRuntime.enterRelayDispatch(
            new ZLinkStreamHeader(packetName, java.util.Map.of(), Optional.empty()));
        try {
            actor.relay(payload).toCompletableFuture().join();
        } finally {
            ZLinkSessionActorsRuntime.exitRelayDispatch();
        }
    }

    @ZLinkHandlerGroup("play-channel")
    public static final class EnsureActorHandler {
        private final ZLinkActorManager actors;

        public EnsureActorHandler(ZLinkActorManager actors) {
            this.actors = actors;
        }

        @ZLinkRequest(packetName = "Ensure")
        public String handle(String actorId) {
            return actors.getOrCreate(actorId, "player")
                .thenApply(actor -> actor.actorId())
                .toCompletableFuture()
                .join();
        }
    }

    public static final class GameSession implements ZLinkSession {
        @Override
        public ZLinkSessionContext context() {
            return null;
        }

        @Override
        public void onConnected() {
                    }

        @Override
        public void onDisconnected() {
                    }

        @Override
        public void onError(ZLinkStreamError error) {
                    }
    }
}
