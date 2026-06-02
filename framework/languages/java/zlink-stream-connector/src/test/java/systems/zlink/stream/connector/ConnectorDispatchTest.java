package systems.zlink.stream.connector;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;

final class ConnectorDispatchTest {
    @Test
    void dispatch_invokesCallback() {
        try (ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options(ZLinkStreamDispatchMode.MANUAL))) {
            AtomicInteger handled = new AtomicInteger();
            connector.on("Ping", message -> {
                handled.incrementAndGet();
                assertEquals("Ping", message.packetName());
                assertEquals("42", message.metadata().get("seq"));
                assertEquals("hello", new String(
                    message.payload().payload().toByteArray(),
                    StandardCharsets.UTF_8));
                message.payload().payload().close();
                return CompletableFuture.completedFuture(null);
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
