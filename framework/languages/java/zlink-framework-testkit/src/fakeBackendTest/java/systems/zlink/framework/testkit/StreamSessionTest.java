package systems.zlink.framework.testkit;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CopyOnWriteArrayList;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamDiagnostic;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;

final class StreamSessionTest {
    @Test
    void streamNodeBindsAndAttachesConfiguredActorGatewaySpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.setRouterRoutingId(RoutingId.from("play-node"));
                node.addSpotFactory(GameSpot.class); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
        }

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.context",
                "create.spotNode",
                "spotNode.setRoutingId",
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
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(GameSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamPacket("Join", "hello");
            awaitCondition(() -> GameSession.dispatches.size() == 1);
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
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(GameSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamPacket("First", "one");
            backendFactory.dispatchStreamPacket("Second", "two");

            awaitCondition(() -> GameSession.dispatches.size() == 2);
            assertEquals(List.of("First:one", "Second:two"), GameSession.dispatches);
            assertEquals(1, GameSession.connectedCount);
            assertEquals(0, GameSession.disconnectedCount);
        }
    }

    @Test
    void onError_reportsTransportError_forRemoteDisconnect() {
        GameSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(GameSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamTransportError(111, "before-session");
            assertEquals(List.of(), GameSession.errors);

            backendFactory.dispatchStreamPacket("Join", "hello");
            backendFactory.dispatchStreamTransportError(222, "remote-disconnect");

            awaitCondition(() -> GameSession.errors.size() == 1);
            assertEquals(List.of("TRANSPORT_ERROR:222:remote-disconnect"), GameSession.errors);
            assertEquals(1, GameSession.disconnectedCount);
        }

        assertEquals(1, GameSession.disconnectedCount);
    }

    @Test
    void streamSessionMustExposeRuntimeProvidedContext() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(WrongContextSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            assertThrows(
                ZLinkConfigurationException.class,
                () -> backendFactory.dispatchStreamPacket("Join", "hello"));
        }
    }

    @Test
    void constructorSessionContextExposesClientAndActorsFromFrameworkRuntime() {
        ContextSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("play"); node.setRouterRoutingId(RoutingId.from("play-node"));
                node.addSpotFactory(GameSpot.class); }; };
        options.addActorFactory("player", ActorRuntimeFakeBackendTest.PlayerActorFactory.class);
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.attachActorGateway("play");
            stream.registerSession(ContextSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamRequest("Join", "hello", 42);

            awaitCondition(() -> ContextSession.sessionId != null);
            assertEquals("gateway:fake-session", ContextSession.sessionId);
            awaitCondition(() -> ContextSession.boundCount == 1);
            assertEquals(1, ContextSession.boundCount, backendFactory.calls().toString());
        }

        assertTrue(backendFactory.calls().contains("stream.bindActor.player-1"));
        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("stream.send.fake-session.Notify.")));
        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("stream.reply.fake-session.42.String.")));
    }

    @Test
    void gatewayAttachedSessionContextExposesActorsWithoutLocalActorRuntime() {
        ContextSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh.addNode("session"); node.setRouterRoutingId(RoutingId.from("session-node")); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.attachActorGateway("session");
            stream.registerSession(ContextSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            ContextSession.actorNodeRid = RoutingId.from("session-node");
            backendFactory.dispatchStreamRequest("Join", "hello", 42);

            awaitCondition(() -> ContextSession.sessionId != null);
            assertEquals("gateway:fake-session", ContextSession.sessionId);
            awaitCondition(() -> ContextSession.boundCount == 1);
            assertEquals(1, ContextSession.boundCount, backendFactory.calls().toString());
        }

        assertTrue(backendFactory.calls().contains("stream.bindActor.player-1"));
        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("stream.reply.fake-session.42.String.")));
    }

    @Test
    void sessionPacketDispatcher_handlesRegisteredPacketsAndLetsSessionRelayUnhandledPackets() {
        DispatcherSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(DispatcherSession.class);
            stream.addSessionPacketHandler(AuthenticatePacketHandler.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamRequest("Authenticate", "player-1", 77);
            backendFactory.dispatchStreamPacket("Move", "cell-4");

            awaitCondition(() -> DispatcherSession.handled.size() == 1
                && DispatcherSession.relays.size() == 1);
            assertEquals(List.of("handler:player-1"), DispatcherSession.handled);
            assertEquals(List.of("relay:Move:cell-4"), DispatcherSession.relays);
        }

        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("stream.reply.fake-session.77.String.")));
    }

    @Test
    void sessionSendAndReplyApplyMetadataAndCompression() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(CompressedSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamRequest("Compress", "hello", 91);
        }

        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("stream.send.fake-session.Notify.")
                && call.contains("PAYLOAD_COMPRESSED")
                && call.contains("trace=send-trace")));
        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("stream.reply.fake-session.91.String.")
                && call.contains("PAYLOAD_COMPRESSED")
                && call.contains("trace=reply-trace")));
    }

    @Test
    void inboundCompressedSessionPayloadIsDecompressedBeforeDispatch() {
        GameSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(GameSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamRequest(
                "Compressed",
                Message.from(hex("00636F6D70726573736564")),
                92,
                0x04);

            awaitCondition(() -> GameSession.dispatches.size() == 1);
            assertEquals(List.of("Compressed:compressed"), GameSession.dispatches);
        }
    }

    @Test
    void sessionReply_failsOutsideRequestPacket() {
        ReplyOnlySession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(ReplyOnlySession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamPacket("Notify", "hello");

            awaitCondition(() -> ReplyOnlySession.errorMessage != null);
            assertEquals(
                "Reply is only available while handling a request packet.",
                ReplyOnlySession.errorMessage);
        }
    }

    private static void awaitCondition(java.util.function.BooleanSupplier condition) {
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(2);
        while (System.nanoTime() < deadline) {
            if (condition.getAsBoolean()) {
                return;
            }
            try {
                Thread.sleep(10);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                throw new AssertionError("interrupted while waiting for condition", ex);
            }
        }
    }

    private static byte[] hex(String value) {
        byte[] result = new byte[value.length() / 2];
        for (int i = 0; i < result.length; i++) {
            result[i] = (byte) Integer.parseInt(value.substring(i * 2, i * 2 + 2), 16);
        }
        return result;
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
        static List<String> dispatches = new CopyOnWriteArrayList<>();
        static List<String> errors = new CopyOnWriteArrayList<>();
        private final ZLinkSessionContext context;

        public GameSession(ZLinkSessionContext context) {
            this.context = context;
        }

        static void reset() {
            connectedCount = 0;
            disconnectedCount = 0;
            dispatches = new CopyOnWriteArrayList<>();
            errors = new CopyOnWriteArrayList<>();
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public void onConnected() {
            connectedCount += 1;
                    }

        @Override
        public void onDisconnected() {
            disconnectedCount += 1;
                    }

        @Override
        public void onError(ZLinkStreamError error) {
            errors.add(error.error()
                + ":" + error.diagnostic().map(ZLinkStreamDiagnostic::nativeCode).orElse(0)
                + ":" + error.diagnostic().map(ZLinkStreamDiagnostic::message).orElse(""));
                    }

        @Override
        public void onDispatch(
            ZLinkStreamHeader header,
            Message payload) {
            dispatches.add(header.packetName() + ":" + payload.toUtf8String());
                    }
    }

    public static final class ContextSession implements ZLinkSession {
        static String sessionId;
        static int boundCount;
        static RoutingId actorNodeRid = RoutingId.from("play-node");
        private final ZLinkSessionContext context;

        public ContextSession(ZLinkSessionContext context) {
            this.context = context;
        }

        static void reset() {
            sessionId = null;
            boundCount = 0;
            actorNodeRid = RoutingId.from("play-node");
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public void onConnected() {
            sessionId = context.sessionId();
                    }

        @Override
        public void onDisconnected() {
                    }

        @Override
        public void onError(ZLinkStreamError error) {
                    }

        @Override
        public void onDispatch(
            ZLinkStreamHeader header,
            Message payload) {
            context.actors()
                .bind(new ZLinkActorRef(
                    actorNodeRid,
                    "player-1",
                    1))
                .thenCompose(actor -> {
                    boundCount = context.actors().bound().size();
                    return context.client()
                        .send(Message.from("payload").withPacketName("Notify"))
                        .packetName("Notify")
                        .submit();
                })
                .thenCompose(ignored -> context.client()
                    .reply(Message.from("reply").withPacketName("Reply"))
                    .submit())
                .toCompletableFuture()
                .join();
        }
    }

    public static final class DispatcherSession implements ZLinkSession {
        static List<String> handled = new CopyOnWriteArrayList<>();
        static List<String> relays = new CopyOnWriteArrayList<>();
        private final ZLinkSessionContext context;
        private final ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers;

        public DispatcherSession(
            ZLinkSessionContext context,
            ZLinkSessionPacketDispatcher<ZLinkSessionContext> handlers) {
            this.context = context;
            this.handlers = handlers;
        }

        static void reset() {
            handled = new CopyOnWriteArrayList<>();
            relays = new CopyOnWriteArrayList<>();
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
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

        @Override
        public void onDispatch(
            ZLinkStreamHeader header,
            Message payload) {
            boolean handled = handlers.tryHandleAsync(context, header, payload)
                .toCompletableFuture()
                .join();
            if (!handled) {
                relays.add("relay:" + header.packetName()
                    + ":" + payload.toUtf8String());
            }
        }
    }

    public static final class AuthenticatePacketHandler
        implements ZLinkSessionPacketHandler<ZLinkSessionContext> {
        @Override
        public String packetName() {
            return "Authenticate";
        }

        @Override
        public void handle(
            ZLinkSessionContext context,
            ZLinkStreamHeader header,
            Message payload) {
            DispatcherSession.handled.add("handler:" + payload.toUtf8String());
            context.client()
                .reply(Message.from("authenticated").withPacketName("Authenticated"))
                .submit()
                .toCompletableFuture()
                .join();
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
        public void onConnected() {
                    }

        @Override
        public void onDisconnected() {
                    }

        @Override
        public void onError(ZLinkStreamError error) {
                    }

        @Override
        public void onDispatch(
            ZLinkStreamHeader header,
            Message payload) {
            context.client()
                .reply(Message.from("reply").withPacketName("Reply"))
                .submit()
                .whenComplete((ignored, error) -> {
                    if (error != null) {
                        errorMessage = error.getMessage();
                    }
                })
                .toCompletableFuture()
                .join();
        }
    }

    public static final class WrongContextSession implements ZLinkSession {
        public WrongContextSession(ZLinkSessionContext context) {
        }

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

    public static final class CompressedSession implements ZLinkSession {
        private final ZLinkSessionContext context;

        public CompressedSession(ZLinkSessionContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
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

        @Override
        public void onDispatch(
            ZLinkStreamHeader header,
            Message payload) {
            context.client()
                .send(Message.from("notify").withPacketName("Notify"))
                .packetName("Notify")
                .metadata("trace", "send-trace")
                .compress()
                .submit()
                .thenCompose(ignored -> context.client()
                    .reply(Message.from("reply").withPacketName("Reply"))
                    .metadata("trace", "reply-trace")
                    .compress()
                    .submit())
                .toCompletableFuture()
                .join();
        }
    }
}
