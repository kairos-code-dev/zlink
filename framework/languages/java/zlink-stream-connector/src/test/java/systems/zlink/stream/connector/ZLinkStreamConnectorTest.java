package systems.zlink.stream.connector;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletionException;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;

final class ZLinkStreamConnectorTest {
    @Test
    void manualDispatchInvokesRegisteredHandlerOnlyWhenDispatched() {
        try (ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options(ZLinkStreamDispatchMode.MANUAL))) {
            AtomicInteger handled = new AtomicInteger();
            connector.on("Ping", message -> {
                handled.incrementAndGet();
                assertEquals("Ping", message.packetName());
                assertEquals("42", message.metadata().get("seq"));
                message.payload().payload().close();
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            connector.connectAsync().toCompletableFuture().join();
            connector.send(payload("Ping", "hello"))
                .metadata("seq", "42")
                .submitAsync()
                .toCompletableFuture()
                .join();

            assertEquals(1, connector.pendingDispatchCount());
            assertEquals(0, handled.get());

            connector.dispatchAsync().toCompletableFuture().join();

            assertEquals(0, connector.pendingDispatchCount());
            assertEquals(1, handled.get());
        }
    }

    @Test
    void requestReturnsPayloadCopyAndQueuesManualDispatch() {
        try (ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options(ZLinkStreamDispatchMode.MANUAL))) {
            connector.on("EchoOverride", message ->
                java.util.concurrent.CompletableFuture.completedFuture(null));
            connector.connectAsync().toCompletableFuture().join();

            ZLinkStreamEncodedPayload reply = connector.request(payload("Echo", "hello"))
                .packetName("EchoOverride")
                .timeout(Duration.ofMillis(500))
                .submitAsync()
                .toCompletableFuture()
                .join();
            try {
                assertEquals("EchoOverride", reply.packetName());
                assertEquals("hello", new String(
                    reply.payload().toByteArray(),
                    StandardCharsets.UTF_8));
                assertEquals(1, connector.pendingDispatchCount());
            } finally {
                reply.payload().close();
            }
        }
    }

    @Test
    void requestWithoutReplyHandlerFailsWithTimeoutCause() {
        try (ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options(ZLinkStreamDispatchMode.MANUAL))) {
            connector.connectAsync().toCompletableFuture().join();

            CompletionException ex = assertThrows(CompletionException.class, () ->
                connector.request(payload("MissingReply", "hello"))
                    .timeout(Duration.ofMillis(10))
                    .submitAsync()
                    .toCompletableFuture()
                    .join());

            assertTrue(ex.getCause() instanceof java.util.concurrent.TimeoutException);
            assertEquals(0, connector.pendingDispatchCount());
        }
    }

    @Test
    void connectAndCloseUpdateState() {
        try (ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options(ZLinkStreamDispatchMode.AUTO))) {
            assertFalse(connector.isConnected());
            assertEquals(ZLinkStreamDispatchMode.AUTO, connector.options().dispatchMode());
            connector.connectAsync().toCompletableFuture().join();
            assertTrue(connector.isConnected());
            connector.closeAsync().toCompletableFuture().join();
            assertEquals(ZLinkStreamConnectionState.CLOSED, connector.state());
        }
    }

    @Test
    void reconnectRestoresConnectionAfterTransportDisconnect() {
        try (ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options(ZLinkStreamDispatchMode.AUTO))) {
            List<ZLinkStreamConnectionState> states = new ArrayList<>();
            connector.onConnectionStateChanged(state -> {
                states.add(state);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            connector.connectAsync().toCompletableFuture().join();
            connector.disconnectAsync().toCompletableFuture().join();
            assertFalse(connector.isConnected());

            connector.reconnectAsync().toCompletableFuture().join();

            assertTrue(connector.isConnected());
            assertEquals(List.of(
                ZLinkStreamConnectionState.CONNECTED,
                ZLinkStreamConnectionState.DISCONNECTED,
                ZLinkStreamConnectionState.RECONNECTING,
                ZLinkStreamConnectionState.CONNECTED), states);
        }
    }

    @Test
    void lifecycleHandlersObserveStateChangesAndDisconnect() throws Exception {
        try (ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options(ZLinkStreamDispatchMode.AUTO))) {
            List<ZLinkStreamConnectionState> states = new ArrayList<>();
            AtomicInteger disconnected = new AtomicInteger();

            AutoCloseable stateRegistration = connector.onConnectionStateChanged(state -> {
                states.add(state);
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });
            AutoCloseable disconnectedRegistration = connector.onDisconnected(() -> {
                disconnected.incrementAndGet();
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            connector.connectAsync().toCompletableFuture().join();
            connector.closeAsync().toCompletableFuture().join();

            assertEquals(List.of(
                ZLinkStreamConnectionState.CONNECTED,
                ZLinkStreamConnectionState.CLOSED), states);
            assertEquals(1, disconnected.get());

            stateRegistration.close();
            disconnectedRegistration.close();
            connector.closeAsync().toCompletableFuture().join();

            assertEquals(2, states.size());
            assertEquals(1, disconnected.get());
        }
    }

    @Test
    void reservedPacketNamesAreRejectedForUserHandlers() {
        try (ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options(ZLinkStreamDispatchMode.MANUAL))) {
            assertThrows(IllegalArgumentException.class,
                () -> connector.on("__zlink.heartbeat", message ->
                    java.util.concurrent.CompletableFuture.completedFuture(null)));
            assertThrows(IllegalArgumentException.class,
                () -> connector.send(payload("__zlink.send", "hello")));
            assertThrows(IllegalArgumentException.class,
                () -> connector.request(payload("__zlink.request", "hello")));
            assertThrows(IllegalArgumentException.class,
                () -> connector.send(payload("Ping", "hello")).packetName("__zlink.override"));
            assertThrows(IllegalArgumentException.class,
                () -> connector.request(payload("Ping", "hello")).packetName("__zlink.override"));
        }
    }

    @Test
    void packetNameLongerThanOneByteLengthIsRejected() {
        try (ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options(ZLinkStreamDispatchMode.MANUAL))) {
            String tooLong = "a".repeat(256);

            assertThrows(IllegalArgumentException.class,
                () -> connector.on(tooLong, message ->
                    java.util.concurrent.CompletableFuture.completedFuture(null)));
            assertThrows(IllegalArgumentException.class,
                () -> connector.send(payload(tooLong, "hello")));
            assertThrows(IllegalArgumentException.class,
                () -> connector.request(payload(tooLong, "hello")));
        }
    }

    @Test
    void uriSchemeAndTransportMismatchIsRejected() {
        assertThrows(IllegalArgumentException.class, () -> ZLinkStreamConnectorFactory.create(
            new ZLinkStreamConnectorOptions(
                URI.create("http://127.0.0.1:7000"),
                ZLinkStreamDispatchMode.MANUAL,
                Duration.ofSeconds(1),
                1)));
        assertThrows(IllegalArgumentException.class, () -> ZLinkStreamConnectorFactory.create(
            new ZLinkStreamConnectorOptions(
                URI.create("127.0.0.1:7000"),
                ZLinkStreamDispatchMode.MANUAL,
                Duration.ofSeconds(1),
                1)));
    }

    private static ZLinkStreamConnectorOptions options(
        ZLinkStreamDispatchMode dispatchMode) {
        return new ZLinkStreamConnectorOptions(
            URI.create("tcp://127.0.0.1:7000"),
            dispatchMode,
            Duration.ofSeconds(1),
            1);
    }

    private static ZLinkStreamEncodedPayload payload(String packetName, String body) {
        return new ZLinkStreamEncodedPayload(
            packetName,
            Message.from(body),
            Map.of());
    }
}
