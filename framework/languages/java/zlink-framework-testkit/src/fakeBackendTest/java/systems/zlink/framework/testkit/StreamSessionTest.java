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
import com.google.protobuf.StringValue;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkEncodedPayload;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.codecs.msgpack.ZLinkMessagePackCodec;
import systems.zlink.framework.codecs.protobuf.ZLinkProtobufCodec;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.configuration.ZLinkCodecRegistration;
import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkSessionPacketDispatcher;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;
import systems.zlink.framework.streams.ZLinkStreamCompressionCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamDiagnostic;
import systems.zlink.framework.streams.ZLinkStreamError;

final class StreamSessionTest {
    @Test
    void streamNodeBindsAndAttachesConfiguredSessionRelaySpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://stream-relay-play-node");
                node.setRoutingId(RoutingId.from("play-node"));
                node.addSpotFactory(GameSpot.class); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(GameSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
        }

        RuntimeTestSupport.awaitClosed(backendFactory);

        assertEquals(
            List.of(
                "factory.channel",
                "create.context",
                "factory.channel",
                "factory.spot",
                "create.spotNode",
                "spotNode.setRoutingId",
                "spotNode.setRouterBind.inproc://stream-relay-play-node",
                "spotNode.entrySpot",
                "create.entrySpot",
                "factory.stream",
                "create.stream",
                "stream.bind.inproc://gateway",
                "stream.onPacket",
                "stream.onTransportError",
                "stream.startSessionService",
                "close.stream",
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

        awaitCondition(() -> GameSession.disconnectedCount == 1);
        assertEquals(1, GameSession.disconnectedCount);
    }

    @Test
    void heartbeatControlPingRepliesWithoutSessionDispatch() {
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

            backendFactory.dispatchStreamControl("$zlink.heartbeat.ping");

            awaitCondition(() -> backendFactory.calls().stream()
                .anyMatch(call -> call.contains("send.fake-session.$zlink.heartbeat.pong")));
            assertEquals(List.of("Join:hello"), GameSession.dispatches);
            assertEquals(1, GameSession.connectedCount);
            assertEquals(0, GameSession.disconnectedCount);
        }
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
    void streamTlsServerIsAppliedBeforeBind() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var stream = options.addStreamNode("gateway"); stream.bind("tcp://127.0.0.1:7776");
            stream.bind("tls://127.0.0.1:7777");
            stream.setTlsServer("server.crt", "server.key", true);
            stream.registerSession(GameSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            awaitCondition(() -> backendFactory.calls().contains("stream.bind.tls://127.0.0.1:7777"));
        }

        int tlsIndex = backendFactory.calls()
            .indexOf("stream.setTlsServer.server.crt.server.key.true");
        int plainBindIndex = backendFactory.calls()
            .indexOf("stream.bind.tcp://127.0.0.1:7776");
        int bindIndex = backendFactory.calls()
            .indexOf("stream.bind.tls://127.0.0.1:7777");
        assertTrue(tlsIndex >= 0, "TLS server setup call was not recorded");
        assertTrue(plainBindIndex >= 0, "plain stream bind call was not recorded");
        assertTrue(bindIndex >= 0, "stream bind call was not recorded");
        assertTrue(tlsIndex < plainBindIndex, "TLS server setup must happen before plain bind");
        assertTrue(tlsIndex < bindIndex, "TLS server setup must happen before TLS bind");
    }

    @Test
    void sessionStateIsRegisteredBeforeOnConnectedCanReenterStreamDispatch() {
        ReentrantOnConnectedSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.useHandlerExecutor(Runnable::run);
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(ReentrantOnConnectedSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        ReentrantOnConnectedSession.backendFactory = backendFactory;

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamPacket("Join", "hello");

            awaitCondition(() -> ReentrantOnConnectedSession.dispatches.size() == 2);
            assertTrue(ReentrantOnConnectedSession.dispatches.contains("Welcome:connected"));
            assertTrue(ReentrantOnConnectedSession.dispatches.contains("Join:hello"));
            assertEquals(1, ReentrantOnConnectedSession.connectedCount);
        }
    }

    @Test
    void customCodecSessionDispatchDecodesThroughFrameworkMessage() {
        GameSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.codecs().use(codecs -> {
            codecs.addSerializer("application/x-session-test", new SessionCustomSerializer());
            codecs.addStreamCodec("application/x-session-test", ZLinkStreamCodec.PROTOBUF);
        });
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(GameSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            Message payload = Message.from("custom:hello");
            try {
                backendFactory.dispatchStreamPacket("Custom", payload, ZLinkStreamCodec.PROTOBUF);
            } finally {
                payload.close();
            }

            awaitCondition(() -> GameSession.dispatches.size() == 1);
            assertEquals(List.of("Custom:hello"), GameSession.dispatches);
        }
    }

    @Test
    void protobufSessionDispatchDecodesThroughFrameworkMessage() {
        ProtobufSession.dispatches = new CopyOnWriteArrayList<>();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.codecs().use(ZLinkProtobufCodec.defaultCodec());
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(ProtobufSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        ZLinkMessageSerializer serializer = serializerWith(ZLinkProtobufCodec.defaultCodec());

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            Message payload = messageFrom(serializer.serialize(StringValue.of("proto-session")));
            try {
                backendFactory.dispatchStreamPacket("Proto", payload, ZLinkStreamCodec.PROTOBUF);
            } finally {
                payload.close();
            }

            awaitCondition(() -> ProtobufSession.dispatches.size() == 1);
            assertEquals(List.of("Proto:proto-session"), ProtobufSession.dispatches);
        }
    }

    @Test
    void messagePackSessionDispatchDecodesThroughFrameworkMessage() {
        MessagePackSession.dispatches = new CopyOnWriteArrayList<>();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.codecs().use(ZLinkMessagePackCodec.defaultCodec());
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(MessagePackSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();
        ZLinkMessageSerializer serializer = serializerWith(ZLinkMessagePackCodec.defaultCodec());

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            Message payload = messageFrom(serializer.serialize(new PackedSessionPayload("msgpack-session")));
            try {
                backendFactory.dispatchStreamPacket("MsgPack", payload, ZLinkStreamCodec.MESSAGE_PACK);
            } finally {
                payload.close();
            }

            awaitCondition(() -> MessagePackSession.dispatches.size() == 1);
            assertEquals(List.of("MsgPack:msgpack-session"), MessagePackSession.dispatches);
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
    void normalPeerDisconnectDoesNotReportTransportError() {
        GameSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(GameSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamPacket("Join", "hello");
            backendFactory.dispatchStreamTransportError(0, "DISCONNECTED");

            awaitCondition(() -> GameSession.disconnectedCount == 1);
            assertEquals(List.of(), GameSession.errors);
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
        { var mesh = options.addSpotMesh("game"); { var node = mesh;
                node.enableRouter("inproc://stream-context-play-node");
                node.setRoutingId(RoutingId.from("play-node"));
                node.addSpotFactory(GameSpot.class);
                node.addActorFactory("player", ActorRuntimeFakeBackendTest.PlayerActorFactory.class); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
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
            awaitCondition(() -> backendFactory.calls().stream()
                .anyMatch(call -> call.startsWith("stream.reply.fake-session.42.String.")));
            awaitCondition(() -> backendFactory.calls().stream()
                .anyMatch(call -> call.startsWith("stream.send.fake-session.String.")));
        }

        assertTrue(backendFactory.calls().contains("stream.bindActor.player-1"));
        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("stream.send.fake-session.String.")));
        assertTrue(backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("stream.reply.fake-session.42.String.")));
    }

    @Test
    void gatewayAttachedSessionContextExposesActorsWithoutLocalActorRuntime() {
        ContextSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var mesh = options.addSpotMesh("game"); { var node = mesh
                .enableRouter("inproc://session-node")
                .setRoutingId(RoutingId.from("session-node")); }; };
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
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
            awaitCondition(() -> backendFactory.calls().stream()
                .anyMatch(call -> call.startsWith("stream.reply.fake-session.42.String.")));
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
            backendFactory.dispatchStreamRequest(
                "Authenticate",
                Message.from("{\"value\":\"player-1\"}".getBytes(
                    java.nio.charset.StandardCharsets.UTF_8)),
                77,
                0);
            backendFactory.dispatchStreamPacket("Move", "cell-4");

            awaitCondition(() -> DispatcherSession.handled.size() == 1
                && DispatcherSession.relays.size() == 1);
            assertEquals(List.of("handler:player-1"), DispatcherSession.handled);
            assertEquals(List.of("relay:Move:cell-4"), DispatcherSession.relays);
            awaitCondition(() -> backendFactory.calls().stream()
                .anyMatch(call -> call.startsWith("stream.reply.fake-session.77.String.")));
        }

    }

    @Test
    void sessionSendAppliesMetadataAndRepliesDoNotCopyIt() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(CompressedSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamRequest("Compress", "hello", 91);
        }

        awaitCondition(() -> backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("stream.send.fake-session.Notify.")
                && call.contains("PAYLOAD_COMPRESSED")
                && call.contains("trace=send-trace")));
        awaitCondition(() -> backendFactory.calls().stream()
            .anyMatch(call -> call.startsWith("stream.reply.fake-session.91.String.")
                && call.contains("PAYLOAD_COMPRESSED")
                && !call.contains("trace=")));
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
                Message.from(hex("0022636F6D7072657373656422")),
                92,
                0x04);

            awaitCondition(() -> GameSession.dispatches.size() == 1);
            assertEquals(List.of("Compressed:compressed"), GameSession.dispatches);
        }
    }

    @Test
    void customStreamCompressionCodecIsUsedForSessionSendReplyAndDispatch() {
        CustomCompressionSession.reset();
        PrefixCompressionCodec codec = new PrefixCompressionCodec("fw");
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.configureStreamCompression().use(codec);
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(CustomCompressionSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamRequest(
                "CustomCompressed",
                Message.from(codec.compress("\"inbound\"".getBytes(java.nio.charset.StandardCharsets.UTF_8))),
                93,
                0x04);

            awaitCondition(() -> CustomCompressionSession.dispatches.size() == 1);
            assertEquals(List.of("CustomCompressed:inbound"), CustomCompressionSession.dispatches);
            awaitCondition(() -> backendFactory.calls().stream()
                .anyMatch(call -> call.startsWith("stream.send.fake-session.String.")
                    && call.contains("PAYLOAD_COMPRESSED")));
            awaitCondition(() -> backendFactory.calls().stream()
                .anyMatch(call -> call.startsWith("stream.reply.fake-session.93.String.")
                    && call.contains("PAYLOAD_COMPRESSED")));
            assertTrue(backendFactory.calls().stream()
                .anyMatch(call -> call.startsWith("stream.send.fake-session.String.")
                    && call.contains("PAYLOAD_COMPRESSED")
                    && call.endsWith(".fw:\"notify\"")), backendFactory.calls().toString());
            assertTrue(backendFactory.calls().stream()
                .anyMatch(call -> call.startsWith("stream.reply.fake-session.93.String.")
                    && call.contains("PAYLOAD_COMPRESSED")
                    && call.endsWith(".fw:\"reply\"")), backendFactory.calls().toString());
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

    @Test
    void sessionRequestFailureRepliesWithStreamError() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(FailingRequestSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamRequest("MustFail", "payload", 44);

            awaitCondition(() -> backendFactory.calls().stream()
                .anyMatch(call -> call.startsWith("stream.reply.fake-session.44.MustFail.")
                    && call.contains("HAS_REQUEST_SEQUENCE")
                    && call.endsWith(".public failure")));
        }
    }

    @Test
    void sessionRequestFailureDoesNotBlockLaterPackets() {
        FailingThenRecoveringSession.reset();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        { var stream = options.addStreamNode("gateway"); stream.bind("inproc://gateway");
            stream.registerSession(FailingThenRecoveringSession.class); };
        FakeZLinkBackendAdapterFactory backendFactory =
            new FakeZLinkBackendAdapterFactory();

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, backendFactory)) {
            backendFactory.dispatchStreamRequest("MustFail", "payload", 44);
            awaitCondition(() -> backendFactory.calls().stream()
                .anyMatch(call -> call.startsWith("stream.reply.fake-session.44.MustFail.")
                    && call.endsWith(".public failure")));

            backendFactory.dispatchStreamRequest("Recover", "next", 45);

            awaitCondition(() -> FailingThenRecoveringSession.dispatches.contains("Recover:next"));
            awaitCondition(() -> backendFactory.calls().stream()
                .anyMatch(call -> call.startsWith("stream.reply.fake-session.45.String.")
                    && call.endsWith(".recovered")));
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

    public static final class GameSpot extends TestZLinkSpot<ZLinkActor> {
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
        public CompletionStage<Void> onConnected() {
            connectedCount += 1;
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            disconnectedCount += 1;
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            errors.add(error.error()
                + ":" + error.diagnostic().map(ZLinkStreamDiagnostic::nativeCode).orElse(0)
                + ":" + error.diagnostic().map(ZLinkStreamDiagnostic::message).orElse(""));
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            dispatches.add(dispatch.packetName() + ":" + payload.decode(String.class));
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class FailingThenRecoveringSession implements ZLinkSession {
        static List<String> dispatches = new CopyOnWriteArrayList<>();
        private final ZLinkSessionContext context;

        public FailingThenRecoveringSession(ZLinkSessionContext context) {
            this.context = context;
        }

        static void reset() {
            dispatches = new CopyOnWriteArrayList<>();
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            if ("MustFail".equals(dispatch.packetName())) {
                throw new IllegalStateException("public failure");
            }
            dispatches.add(dispatch.packetName() + ":" + payload.decode(String.class));
            context.client().reply("recovered").submit();
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class ReentrantOnConnectedSession implements ZLinkSession {
        static FakeZLinkBackendAdapterFactory backendFactory;
        static int connectedCount;
        static boolean dispatchedFromConnect;
        static List<String> dispatches = new CopyOnWriteArrayList<>();
        private final ZLinkSessionContext context;

        public ReentrantOnConnectedSession(ZLinkSessionContext context) {
            this.context = context;
        }

        static void reset() {
            backendFactory = null;
            connectedCount = 0;
            dispatchedFromConnect = false;
            dispatches = new CopyOnWriteArrayList<>();
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onConnected() {
            connectedCount += 1;
            if (!dispatchedFromConnect) {
                dispatchedFromConnect = true;
                backendFactory.dispatchStreamPacket("Welcome", "connected");
            }
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            dispatches.add(dispatch.packetName() + ":" + payload.decode(String.class));
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class ProtobufSession implements ZLinkSession {
        static List<String> dispatches = new CopyOnWriteArrayList<>();
        private final ZLinkSessionContext context;

        public ProtobufSession(ZLinkSessionContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            dispatches.add(dispatch.packetName() + ":" + payload.decode(StringValue.class).getValue());
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class MessagePackSession implements ZLinkSession {
        static List<String> dispatches = new CopyOnWriteArrayList<>();
        private final ZLinkSessionContext context;

        public MessagePackSession(ZLinkSessionContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            dispatches.add(dispatch.packetName() + ":" + payload.decode(PackedSessionPayload.class).value());
            return CompletableFuture.completedFuture(null);
        }
    }

    public record PackedSessionPayload(String value) {
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
        public CompletionStage<Void> onConnected() {
            sessionId = context.sessionId();
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            return context.actors()
                .bind(new ActorRef(
                    actorNodeRid,
                    "player-1",
                    1))
                .thenCompose(actor -> {
                    boundCount = context.actors().bound().size();
                    context.client().send("payload").submit();
                    context.client().reply("reply").submit();
                    return CompletableFuture.completedFuture(null);
                });
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
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            return handlers.tryHandle(context, dispatch, payload).thenAccept(handled -> {
                if (!handled) {
                    relays.add("relay:" + dispatch.packetName()
                        + ":" + payload.decode(String.class));
                }
            });
        }
    }

    public static final class AuthenticatePacketHandler
        implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, AuthenticatePacket> {
        @Override
        public Class<AuthenticatePacket> messageType() {
            return AuthenticatePacket.class;
        }

        @Override
        public CompletionStage<Void> handle(
            ZLinkSessionContext context,
            ZLinkSessionDispatchContext dispatch,
            AuthenticatePacket payload) {
            DispatcherSession.handled.add("handler:" + payload.value());
            context.client()
                .reply("authenticated")
                .submit();
            return CompletableFuture.completedFuture(null);
        }
    }

    @systems.zlink.framework.handlers.ZLinkPacket("Authenticate")
    public record AuthenticatePacket(String value) {
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
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            try {
                context.client()
                    .reply("reply")
                    .submit();
            } catch (RuntimeException error) {
                errorMessage = error.getMessage();
            }
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class FailingRequestSession implements ZLinkSession {
        private final ZLinkSessionContext context;

        public FailingRequestSession(ZLinkSessionContext context) {
            this.context = context;
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            return CompletableFuture.failedFuture(new IllegalStateException("public failure"));
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
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
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
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            context.client().send("notify")
                .metadata("trace", "send-trace").compress().submit();
            context.client().reply("reply")
                .compress().submit();
            return CompletableFuture.completedFuture(null);
        }
    }

    public static final class CustomCompressionSession implements ZLinkSession {
        static final List<String> dispatches = new CopyOnWriteArrayList<>();
        private final ZLinkSessionContext context;

        public CustomCompressionSession(ZLinkSessionContext context) {
            this.context = context;
        }

        static void reset() {
            dispatches.clear();
        }

        @Override
        public ZLinkSessionContext context() {
            return context;
        }

        @Override
        public CompletionStage<Void> onConnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnected() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onError(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDispatch(
            ZLinkSessionDispatchContext dispatch,
            ZLinkMessage payload) {
            dispatches.add(dispatch.packetName() + ":" + payload.decode(String.class));
            context.client().send("notify").compress().submit();
            context.client().reply("reply").compress().submit();
            return CompletableFuture.completedFuture(null);
        }
    }

    private record PrefixCompressionCodec(String prefix) implements ZLinkStreamCompressionCodec {
        @Override
        public byte[] compress(byte[] payload) {
            return (prefix + ":" + new String(payload, java.nio.charset.StandardCharsets.UTF_8))
                .getBytes(java.nio.charset.StandardCharsets.UTF_8);
        }

        @Override
        public byte[] decompress(byte[] payload, int maxDecompressedSize) {
            String value = new String(payload, java.nio.charset.StandardCharsets.UTF_8);
            String marker = prefix + ":";
            if (!value.startsWith(marker)) {
                throw new IllegalArgumentException("unexpected compression marker");
            }
            return value.substring(marker.length()).getBytes(java.nio.charset.StandardCharsets.UTF_8);
        }
    }

    public static final class SessionCustomSerializer implements ZLinkMessageSerializer {
        @Override
        public <T> ZLinkEncodedPayload serialize(T value) {
            if (value instanceof Message message) {
                return ZLinkEncodedPayload.from(message.toByteArray());
            }
            return ZLinkEncodedPayload.from(("custom:" + value).getBytes(java.nio.charset.StandardCharsets.UTF_8));
        }

        @Override
        public <T> T deserialize(ZLinkEncodedPayload payload, Class<T> type) {
            String value = new String(payload.bytes(), java.nio.charset.StandardCharsets.UTF_8);
            String decoded = value.startsWith("custom:") ? value.substring("custom:".length()) : value;
            if (type == String.class) {
                return type.cast(decoded);
            }
            throw new IllegalArgumentException("unsupported message type: " + type.getName());
        }
    }

    private static ZLinkMessageSerializer serializerWith(
        systems.zlink.framework.configuration.ZLinkCodecExtension extension) {
        ZLinkCodecRegistration registration = new ZLinkCodecRegistration();
        registration.use(extension);
        return registration.serializerWithFallback(new ZLinkJsonMessageSerializer());
    }

    private static Message messageFrom(ZLinkEncodedPayload payload) {
        return Message.from(payload.bytes());
    }
}
