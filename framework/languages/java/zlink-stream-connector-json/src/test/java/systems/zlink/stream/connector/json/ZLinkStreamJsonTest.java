package systems.zlink.stream.connector.json;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.net.URI;
import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.stream.connector.ZLinkStreamConnectionState;
import systems.zlink.stream.connector.ZLinkStreamConnectionStateHandler;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamCodec;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.ZLinkStreamDisconnectedHandler;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;
import systems.zlink.stream.connector.ZLinkStreamErrorHandler;
import systems.zlink.stream.connector.ZLinkStreamMessageHandler;
import systems.zlink.stream.connector.ZLinkStreamPacketName;
import systems.zlink.stream.connector.ZLinkStreamRequestCall;
import systems.zlink.stream.connector.ZLinkStreamSendCall;

final class ZLinkStreamJsonTest {
    @Test
    void typedSendUsesPacketNameAnnotationByDefault() {
        FakeConnector connector = new FakeConnector(options());

        ZLinkStreamJson.send(connector, new AnnotatedPayload("hello"));

        assertEquals("custom.packet", connector.sent.packetName());
        assertEquals(ZLinkStreamCodec.JSON, connector.sent.codec());
    }

    @Test
    void typedOnUsesConnectorNameResolver() {
        FakeConnector connector = new FakeConnector(new ZLinkStreamConnectorOptions(
            URI.create("tcp://127.0.0.1:1"),
            ZLinkStreamDispatchMode.MANUAL,
            Duration.ofSeconds(1),
            1,
            Duration.ofSeconds(1),
            64 * 1024,
            true,
            Duration.ofSeconds(1),
            Duration.ofSeconds(5),
            true,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            false,
            payloadType -> "resolved." + payloadType.getSimpleName()));

        ZLinkStreamJson.on(
            connector,
            AnnotatedPayload.class,
            message -> CompletableFuture.completedFuture(null));

        assertEquals("resolved.AnnotatedPayload", connector.handlerName);
    }

    private static ZLinkStreamConnectorOptions options() {
        return new ZLinkStreamConnectorOptions(
            URI.create("tcp://127.0.0.1:1"),
            ZLinkStreamDispatchMode.MANUAL,
            Duration.ofSeconds(1),
            1);
    }

    @ZLinkStreamPacketName("custom.packet")
    private record AnnotatedPayload(String value) {
    }

    private static final class FakeConnector implements ZLinkStreamConnector {
        private final ZLinkStreamConnectorOptions options;
        private ZLinkStreamEncodedPayload sent;
        private String handlerName;

        FakeConnector(ZLinkStreamConnectorOptions options) {
            this.options = options;
        }

        @Override
        public boolean isConnected() {
            return true;
        }

        @Override
        public ZLinkStreamConnectionState state() {
            return ZLinkStreamConnectionState.CONNECTED;
        }

        @Override
        public ZLinkStreamConnectorOptions options() {
            return options;
        }

        @Override
        public int pendingDispatchCount() {
            return 0;
        }

        @Override
        public CompletionStage<Void> connectAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> disconnectAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> reconnectAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> closeAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> dispatchAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public ZLinkStreamSendCall send(ZLinkStreamEncodedPayload payload) {
            sent = payload;
            return new NoopSendCall();
        }

        @Override
        public ZLinkStreamRequestCall request(ZLinkStreamEncodedPayload payload) {
            throw new UnsupportedOperationException();
        }

        @Override
        public AutoCloseable on(
            String name,
            ZLinkStreamMessageHandler<ZLinkStreamEncodedPayload> handler) {
            handlerName = name;
            return () -> { };
        }

        @Override
        public AutoCloseable onErrorReceived(ZLinkStreamErrorHandler handler) {
            return () -> { };
        }

        @Override
        public AutoCloseable onDisconnected(ZLinkStreamDisconnectedHandler handler) {
            return () -> { };
        }

        @Override
        public AutoCloseable onConnectionStateChanged(ZLinkStreamConnectionStateHandler handler) {
            return () -> { };
        }

        @Override
        public void close() {
        }
    }

    private static final class NoopSendCall implements ZLinkStreamSendCall {
        @Override
        public ZLinkStreamSendCall packetName(String packetName) {
            return this;
        }

        @Override
        public ZLinkStreamSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkStreamSendCall metadata(Map<String, String> metadata) {
            return this;
        }

        @Override
        public ZLinkStreamSendCall compress() {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.completedFuture(null);
        }
    }
}
