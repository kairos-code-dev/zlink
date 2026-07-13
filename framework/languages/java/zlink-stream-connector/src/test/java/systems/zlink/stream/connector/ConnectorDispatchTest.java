package systems.zlink.stream.connector;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.messaging.Message;

final class ConnectorDispatchTest {
    @Test
    void dispatch_invokesCallback() throws Exception {
        try (TcpStreamConnectorTestServer server = new TcpStreamConnectorTestServer()) {
            ZLinkStreamConnector connector =
                ZLinkStreamConnectorFactory.create(server.options(ZLinkStreamDispatchMode.MANUAL));
            try {
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

            ConnectorTestAwait.await(connector.connect());
            server.sendAsync(new ZLinkStreamWireProtocol.Header(
                    ZLinkStreamWireProtocol.KIND_SEND,
                    ZLinkStreamWireProtocol.CODEC_RAW,
                    ZLinkStreamWireProtocol.FLAG_HAS_METADATA,
                    null,
                    "Ping",
                    Map.of("seq", "42"),
            null),
                TcpStreamConnectorTestServer.bytes("hello")).join();

            TcpStreamConnectorTestServer.awaitCondition(
                () -> connector.pendingDispatchCount() == 1);
            assertEquals(1, connector.pendingDispatchCount());
            assertEquals(0, handled.get());

            ConnectorTestAwait.await(connector.dispatch());

            TcpStreamConnectorTestServer.awaitCondition(
                () -> connector.pendingDispatchCount() == 0 && handled.get() == 1);
            assertEquals(1, handled.get());
            } finally {
                ConnectorTestAwait.await(connector.close());
            }
        }
    }

    @Test
    void dispatchQueueKeepsNewestReceivedMessagesWhenBounded() {
        ZLinkStreamDispatchQueue queue = new ZLinkStreamDispatchQueue(1);
        java.util.List<String> handled = new java.util.ArrayList<>();

        queue.add("Push", () -> handled.add("push-0"));
        queue.add("Push", () -> handled.add("push-1"));
        queue.add("Push", () -> handled.add("push-2"));

        assertEquals(1, queue.size());
        assertEquals(1, queue.receivedCount("Push"));

        queue.drain();

        assertEquals(java.util.List.of("push-2"), handled);
        assertEquals(0, queue.receivedCount("Push"));
    }

    private static ZLinkStreamEncodedPayload payload(String packetName, String body) {
        return new ZLinkStreamEncodedPayload(
            packetName,
            Message.from(body),
            Map.of());
    }
}
