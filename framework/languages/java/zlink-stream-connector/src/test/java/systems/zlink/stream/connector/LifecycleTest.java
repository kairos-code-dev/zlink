package systems.zlink.stream.connector;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.net.URI;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletionException;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;

final class LifecycleTest {
    @Test
    void heartbeatTimeoutFailsPendingRequestsWithTimeoutCause() {
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
    void reconnectRestoresConnectionAfterTransportClose() {
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
