package systems.zlink.framework.runtime;

import systems.zlink.framework.runtime.configuration.DefaultZLinkFrameworkOptions;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntime;

import systems.zlink.framework.runtime.backend.*;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.InputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.runtime.binding.ZLinkJavaBackendAdapterFactory;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;

final class StreamSessionTest {
    @Test
    void streamNodeAttachActorGatewayUsesJavaBindingPublicApi() {
        Zlink.version();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter();
                node.addSpotFactory(GameSpot.class);
            }));
        options.addStreamNode("gateway", stream -> {
            stream.bind("inproc://gateway-" + System.nanoTime());
            stream.attachActorGateway("play");
            stream.registerSession(GameSession.class);
        });

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory())) {
        }
    }

    @Test
    void streamNodeDispatchesTcpRequestAndReplies() throws Exception {
        Zlink.version();
        EchoSession.reset();
        int port = reservePort();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addStreamNode("gateway", stream -> {
            stream.bind("tcp://127.0.0.1:" + port);
            stream.registerSession(EchoSession.class);
        });

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory());
             Socket client = new Socket("127.0.0.1", port)) {
            client.setSoTimeout(3000);
            client.getOutputStream().write(frame(requestHeader(7L, "Ping"), bytes("ping")));
            client.getOutputStream().flush();

            byte[] prefix = client.getInputStream().readNBytes(6);
            assertEquals(6, prefix.length);
            ByteBuffer prefixBuffer = ByteBuffer.wrap(prefix);
            int headerSize = Short.toUnsignedInt(prefixBuffer.getShort());
            int bodySize = prefixBuffer.getInt();
            byte[] header = readExact(client.getInputStream(), headerSize);
            byte[] body = readExact(client.getInputStream(), bodySize);

            assertTrue(header.length > 0);
            assertEquals(3, Byte.toUnsignedInt(header[0]));
            assertEquals(7L, ByteBuffer.wrap(header, 3, Long.BYTES).getLong());
            assertEquals("pong", new String(body, StandardCharsets.UTF_8));
            assertTrue(EchoSession.dispatchedOnVirtualThread.get());
        }
    }

    @Test
    void streamActorGatewayRelaysRequestAndReplies() throws Exception {
        Zlink.version();
        int port = reservePort();
        DefaultZLinkFrameworkOptions options = new DefaultZLinkFrameworkOptions();
        options.addHandlersFromPackageOf(StreamSessionTest.class);
        options.addSpotMesh("game", mesh ->
            mesh.addNode("play", node -> {
                node.enableRouter(router ->
                    router.setRoutingId(RoutingId.from("play-node")));
                node.addEntrySpot(GameEntrySpot.class);
            }));
        options.addActorFactory("player", PlayerActorFactory.class);
        options.addStreamNode("gateway", stream -> {
            stream.bind("tcp://127.0.0.1:" + port);
            stream.attachActorGateway("play");
            stream.registerSession(ActorRelaySession.class);
        });

        try (ZLinkFrameworkRuntime ignored =
                 RuntimeTestSupport.startFramework(options, new ZLinkJavaBackendAdapterFactory());
             Socket client = new Socket("127.0.0.1", port)) {
            client.setSoTimeout(3000);

            client.getOutputStream().write(frame(requestHeader(1L, "Bind"), bytes("player-1")));
            client.getOutputStream().flush();
            assertReply(client.getInputStream(), 1L, "bound");

            client.getOutputStream().write(frame(requestHeader(2L, "StreamActorEcho"), bytes("hello")));
            client.getOutputStream().flush();
            assertReply(client.getInputStream(), 2L, "player-1:hello");
        }
    }

    public static final class GameSpot implements ZLinkSpot {
        @Override
        public ZLinkSpotContext context() {
            return null;
        }
    }

    public static final class GameEntrySpot implements ZLinkEntrySpot {
        private final ZLinkEntrySpotContext context;

        public GameEntrySpot(ZLinkEntrySpotContext context) {
            this.context = context;
        }

        @Override
        public ZLinkEntrySpotContext context() {
            return context;
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
        public CompletionStage<ZLinkActor> create(
            String actorId,
            ZLinkActorContext context) {
            return CompletableFuture.completedFuture(new PlayerActor(actorId, context));
        }
    }

    public static final class ActorEchoHandler {
        @ZLinkSpotActorRequest(packetName = "StreamActorEcho")
        public CompletionStage<String> handleAsync(PlayerActor actor, String request) {
            return CompletableFuture.completedFuture(actor.actorId() + ":" + request);
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

    public static final class EchoSession implements ZLinkSession {
        static final AtomicBoolean dispatchedOnVirtualThread = new AtomicBoolean();
        private final ZLinkSessionContext context;

        public EchoSession(ZLinkSessionContext context) {
            this.context = context;
        }

        static void reset() {
            dispatchedOnVirtualThread.set(false);
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
            dispatchedOnVirtualThread.set(Thread.currentThread().isVirtual());
            if (!"Ping".equals(header.packetName())) {
                return CompletableFuture.failedFuture(
                    new IllegalArgumentException("unexpected packet: " + header.packetName()));
            }
            return context.client().reply("pong").submit();
        }
    }

    public static final class ActorRelaySession implements ZLinkSession {
        private final ZLinkSessionContext context;
        private final ZLinkActorManager actors;

        public ActorRelaySession(
            ZLinkSessionContext context,
            ZLinkActorManager actors) {
            this.context = context;
            this.actors = actors;
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
            if ("Bind".equals(header.packetName())) {
                String actorId = new String(payload.toByteArray(), StandardCharsets.UTF_8);
                return actors.getOrCreate(actorId, "player")
                    .thenCompose(actor -> actor.context()
                        .joinEntrySpot(RoutingId.from("play-node"))
                        .submit()
                        .thenCompose(ignored -> context.actors().bind(actor)))
                    .thenCompose(ignored -> context.client().reply("bound").submit());
            }
            return context.actors().bound().get(0).relay(header, payload);
        }
    }

    private static int reservePort() throws Exception {
        try (ServerSocket server = new ServerSocket(0)) {
            return server.getLocalPort();
        }
    }

    private static byte[] requestHeader(long requestSeq, String packetName) {
        byte[] name = bytes(packetName);
        ByteBuffer buffer = ByteBuffer.allocate(3 + Long.BYTES + 1 + name.length);
        buffer.put((byte) 2);
        buffer.put((byte) 0);
        buffer.put((byte) 1);
        buffer.putLong(requestSeq);
        buffer.put((byte) name.length);
        buffer.put(name);
        return buffer.array();
    }

    private static byte[] frame(byte[] header, byte[] body) {
        ByteBuffer buffer = ByteBuffer.allocate(6 + header.length + body.length);
        buffer.putShort((short) header.length);
        buffer.putInt(body.length);
        buffer.put(header);
        buffer.put(body);
        return buffer.array();
    }

    private static byte[] readExact(InputStream input, int size) throws Exception {
        byte[] bytes = input.readNBytes(size);
        assertEquals(size, bytes.length);
        return bytes;
    }

    private static void assertReply(InputStream input, long requestSeq, String expectedBody)
        throws Exception {
        byte[] prefix = input.readNBytes(6);
        assertEquals(6, prefix.length);
        ByteBuffer prefixBuffer = ByteBuffer.wrap(prefix);
        int headerSize = Short.toUnsignedInt(prefixBuffer.getShort());
        int bodySize = prefixBuffer.getInt();
        byte[] header = readExact(input, headerSize);
        byte[] body = readExact(input, bodySize);

        assertEquals(3, Byte.toUnsignedInt(header[0]));
        assertEquals(requestSeq, ByteBuffer.wrap(header, 3, Long.BYTES).getLong());
        assertEquals(expectedBody, new String(body, StandardCharsets.UTF_8));
    }

    private static byte[] bytes(String value) {
        return value.getBytes(StandardCharsets.UTF_8);
    }
}
