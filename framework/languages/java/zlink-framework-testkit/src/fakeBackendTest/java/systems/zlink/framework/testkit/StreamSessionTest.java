package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;

final class StreamSessionTest {
    @Test
    void streamNodeBindsAndAttachesConfiguredActorGatewaySpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.addSpotFactory(GameSpot.class);
            }));
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class);
        });
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "factory.channel",
                "factory.stream",
                "create.context",
                "create.stream",
                "stream.bind.inproc://gateway",
                "stream.onPacket",
                "stream.onTransportError",
                "stream.attachActorGateway.spotNode",
                "close.context",
                "close.stream",
                "close.context",
                "close.spotNode",
                "close.context"),
            backendFactory.calls());
    }

    @Test
    void headerSession_connectedDispatchReply_succeeds() {
        GameSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.registerSession(GameSession.class);
        });
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            backendFactory.dispatchStreamPacket("Join", "hello");
            assertEquals(1, GameSession.connectedCount);
            assertEquals(List.of("Join:hello"), GameSession.dispatches);
            assertEquals(0, GameSession.disconnectedCount);
        }

        assertEquals(1, GameSession.disconnectedCount);
    }

    @Test
    void sameSessionCallbacks_runSerially() {
        GameSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.registerSession(GameSession.class);
        });
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            backendFactory.dispatchStreamPacket("First", "one");
            backendFactory.dispatchStreamPacket("Second", "two");

            assertEquals(List.of("First:one", "Second:two"), GameSession.dispatches);
            assertEquals(1, GameSession.connectedCount);
            assertEquals(0, GameSession.disconnectedCount);
        }
    }

    @Test
    void onError_reportsTransportError_forRemoteDisconnect() {
        GameSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.registerSession(GameSession.class);
        });
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            backendFactory.dispatchStreamTransportError(111, "before-session");
            assertEquals(List.of(), GameSession.errors);

            backendFactory.dispatchStreamPacket("Join", "hello");
            backendFactory.dispatchStreamTransportError(222, "remote-disconnect");

            assertEquals(List.of("TRANSPORT_ERROR:222:remote-disconnect"), GameSession.errors);
            assertEquals(0, GameSession.disconnectedCount);
        }

        assertEquals(1, GameSession.disconnectedCount);
    }

    @Test
    void constructorSessionContextExposesClientAndActorsFromFrameworkRuntime() {
        ContextSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.addSpotFactory(GameSpot.class);
            }));
        options.addActorFactory("player", systems.zlink.framework.testkit.ActorRuntimeFakeBackendTest.PlayerActorFactory.class);
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(ContextSession.class);
        });
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            backendFactory.dispatchStreamRequest("Join", "hello", 42);

            assertEquals("gateway:fake-session", ContextSession.sessionId);
            assertEquals(1, ContextSession.boundCount);
        }

        assertTrue(backendFactory.calls().contains("stream.bindActor.player-1"));
        assertTrue(backendFactory.calls().contains("stream.send.fake-session.Notify"));
        assertTrue(backendFactory.calls().contains("stream.reply.fake-session.42.Join.reply"));
    }

    @Test
    void sessionPacketDispatcher_handlesRegisteredPacketsAndLetsSessionRelayUnhandledPackets() {
        DispatcherSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.registerSession(DispatcherSession.class);
            stream.addSessionPacketHandler(AuthenticatePacketHandler.class);
        });
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            backendFactory.dispatchStreamRequest("Authenticate", "player-1", 77);
            backendFactory.dispatchStreamPacket("Move", "cell-4");

            assertEquals(List.of("handler:player-1"), DispatcherSession.handled);
            assertEquals(List.of("relay:Move:cell-4"), DispatcherSession.relays);
        }

        assertTrue(backendFactory.calls().contains("stream.reply.fake-session.77.Authenticate.authenticated"));
    }

    @Test
    void sessionReply_failsOutsideRequestPacket() {
        ReplyOnlySession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway");
            stream.registerSession(ReplyOnlySession.class);
        });
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 ZLinkFrameworkRuntime.start(options, backendFactory)) {
            backendFactory.dispatchStreamPacket("Notify", "hello");

            assertEquals(
                "Reply is only available while handling a request packet.",
                ReplyOnlySession.errorMessage);
        }
    }

    public static final class GameSpot implements ZLinkSpot {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }
    }

    public static final class GameSession implements ZLinkSession {
        static int connectedCount;
        static int disconnectedCount;
        static List<String> dispatches = new java.util.ArrayList<>();
        static List<String> errors = new java.util.ArrayList<>();

        static void reset() {
            connectedCount = 0;
            disconnectedCount = 0;
            dispatches = new java.util.ArrayList<>();
            errors = new java.util.ArrayList<>();
        }

        @Override
        public ZLinkSessionContext context() {
            return null;
        }

        @Override
        public CompletionStage<Void> onConnectedAsync() {
            connectedCount += 1;
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnectedAsync() {
            disconnectedCount += 1;
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
            errors.add(error.error()
                + ":" + error.diagnostic().map(systems.zlink.framework.streams.ZLinkStreamDiagnostic::nativeCode).orElse(0)
                + ":" + error.diagnostic().map(systems.zlink.framework.streams.ZLinkStreamDiagnostic::message).orElse(""));
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatchAsync(
            systems.zlink.framework.streams.ZLinkStreamHeader header,
            Message payload) {
            dispatches.add(header.packetName() + ":" + payload.toUtf8String());
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class ContextSession implements ZLinkSession {
        static String sessionId;
        static int boundCount;
        private final ZLinkSessionContext context;

        public ContextSession(ZLinkSessionContext context) {
            this.context = context;
        }

        static void reset() {
            sessionId = null;
            boundCount = 0;
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onConnectedAsync() {
            sessionId = context.sessionId();
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

        @Override
        public CompletionStage<Void> onDispatchAsync(
            systems.zlink.framework.streams.ZLinkStreamHeader header,
            Message payload) {
            return context.actors()
                .bindAsync(new ZLinkActorRef(
                    systems.zlink.contracts.core.RoutingId.from("play-node"),
                    "player-1",
                    1))
                .thenCompose(actor -> {
                    boundCount = context.actors().bound().size();
                    return context.client()
                        .send("payload")
                        .packetName("Notify")
                        .submitAsync();
                })
                .thenCompose(ignored -> context.client()
                    .reply("reply")
                    .submitAsync());
        }
    }

    public static final class DispatcherSession implements ZLinkSession {
        static List<String> handled = new java.util.ArrayList<>();
        static List<String> relays = new java.util.ArrayList<>();
        private final ZLinkSessionContext context;
        private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;

        public DispatcherSession(
            ZLinkSessionContext context,
            ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers) {
            this.context = context;
            this.handlers = handlers;
        }

        static void reset() {
            handled = new java.util.ArrayList<>();
            relays = new java.util.ArrayList<>();
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
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

        @Override
        public CompletionStage<Void> onDispatchAsync(
            ZLinkStreamHeader header,
            Message payload) {
            return handlers.tryHandleAsync(context, header, payload)
                .thenCompose(handled -> {
                    if (handled) {
                        return CompletableFuture.completedFuture(null);
                    }
                    relays.add("relay:" + header.packetName()
                        + ":" + payload.toUtf8String());
                    return CompletableFuture.completedFuture(null);
                });
        }
    }

    public static final class AuthenticatePacketHandler
        implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
        @Override
        public String packetName() {
            return "Authenticate";
        }

        @Override
        public CompletionStage<Void> handleAsync(
            ZLinkSessionContext context,
            ZLinkStreamHeader header,
            Message payload) {
            DispatcherSession.handled.add("handler:" + payload.toUtf8String());
            return context.client()
                .reply("authenticated")
                .submitAsync();
        }
    }

    public static final class ReplyOnlySession implements ZLinkSession {
        static String errorMessage;
        private final ZLinkSessionContext context;

        public ReplyOnlySession(ZLinkSessionContext context) {
            this.context = context;
        }

        static void reset() {
            errorMessage = null;
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
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

        @Override
        public CompletionStage<Void> onDispatchAsync(
            ZLinkStreamHeader header,
            Message payload) {
            return context.client()
                .reply("reply")
                .submitAsync()
                .whenComplete((ignored, error) -> {
                    if (error != null) {
                        errorMessage = error.getMessage();
                    }
                });
        }
    }
}
