package systems.zlink.framework.testkit;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.runtime.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.ZLinkFrameworkRuntime;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;

final class StreamRuntimeFakeBackendTest {
    @Test
    void streamNodeBindsAndAttachesConfiguredActorGatewaySpotNode() {
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> node.addSpotFactory(GameSpot.class)));
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
    void streamPacketDispatchCreatesSessionAndRunsLifecycleCallbacks() {
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
}
