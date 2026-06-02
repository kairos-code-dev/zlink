package systems.zlink.stream.connector;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.nio.charset.StandardCharsets;
import java.util.Map;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;

final class StreamPayloadTest {
    @Test
    void responsePayload_requiresCopyOutsideTransportBuffer() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer();
             ZLinkStreamConnector connector =
                 ZLinkStreamConnectorFactory.create(server.options(ZLinkStreamDispatchMode.MANUAL))) {
            connector.connectAsync().toCompletableFuture().join();

            var requestFrame = server.readFrameAsync();
            var replyFuture = connector.request(payload("Echo", "copy-me"))
                .submitAsync()
                .toCompletableFuture()
                .thenApply(reply -> reply);

            TcpStreamConnectorTestServer.ReceivedFrame request = requestFrame.join();
            server.sendAsync(TcpStreamConnectorTestServer.responseTo(
                    request,
                    "Echo",
                    Map.of()),
                TcpStreamConnectorTestServer.bytes("copy-me")).join();

            ZLinkStreamEncodedPayload reply = replyFuture.join();
            try {
                assertEquals("copy-me", new String(
                    reply.payload().toByteArray(),
                    StandardCharsets.UTF_8));
            } finally {
                reply.payload().close();
            }
        }
    }

    private static ZLinkStreamEncodedPayload payload(String packetName, String body) {
        return new ZLinkStreamEncodedPayload(
            packetName,
            Message.from(body),
            Map.of());
    }
}
