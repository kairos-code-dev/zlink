package systems.zlink.stream.connector;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.Map;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;

final class StreamPayloadTest {
    @Test
    void borrowedPayload_requiresCopyOutsideCallback() {
        try (ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(options())) {
            connector.on("Echo", message -> {
                message.payload().payload().close();
                return java.util.concurrent.CompletableFuture.completedFuture(null);
            });

            connector.connectAsync().toCompletableFuture().join();

            ZLinkStreamEncodedPayload reply = connector.request(payload("Echo", "copy-me"))
                .submitAsync()
                .toCompletableFuture()
                .join();
            try {
                assertEquals("copy-me", new String(
                    reply.payload().toByteArray(),
                    StandardCharsets.UTF_8));
            } finally {
                reply.payload().close();
            }
        }
    }

    private static ZLinkStreamConnectorOptions options() {
        return new ZLinkStreamConnectorOptions(
            URI.create("tcp://127.0.0.1:7000"),
            ZLinkStreamDispatchMode.MANUAL,
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
