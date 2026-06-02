package systems.zlink.samples.streamingclient;

import java.net.URI;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.stream.connector.ZLinkStreamConnectionState;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;

public final class StreamingClientSample {
    public static void main(String[] args) throws Exception {
        List<String> events = new ArrayList<>();
        List<ZLinkStreamConnectionState> states = new ArrayList<>();
        ZLinkStreamConnector connector = createConnector();
        connector.onConnectionStateChanged(state -> {
            states.add(state);
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        });
        connector.onDisconnected(() -> {
            events.add("Disconnected");
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        });
        connector.on("GameInput", message -> {
            events.add(message.packetName() + ":" + message.payload().metadata().get("source"));
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        });
        connector.connectAsync().toCompletableFuture().join();
        require(connector.isConnected(), "connector did not connect");
        require(connector.options().dispatchMode() == ZLinkStreamDispatchMode.MANUAL,
            "connector did not keep manual dispatch option");

        connector.send(payload("GameInput", "move", Map.of("source", "send")))
            .packetName("GameInput")
            .submitAsync()
            .toCompletableFuture()
            .join();
        require(connector.pendingDispatchCount() == 1, "manual dispatch did not queue send");
        connector.dispatchAsync().toCompletableFuture().join();
        require(events.contains("GameInput:send"), "manual send handler not invoked");

        ZLinkStreamEncodedPayload reply = connector.request(payload(
                "GameInput",
                "query",
                Map.of("source", "request")))
            .packetName("GameInput")
            .metadata("requestId", "r1")
            .timeout(Duration.ofSeconds(2))
            .submitAsync()
            .toCompletableFuture()
            .join();
        require(reply.packetName().equals("GameInput"), "request packet name mismatch");
        connector.dispatchAsync().toCompletableFuture().join();
        require(events.contains("GameInput:request"), "manual request handler not invoked");

        connector.closeAsync().toCompletableFuture().join();
        require(connector.state() == ZLinkStreamConnectionState.CLOSED, "connector did not close");
        require(states.equals(List.of(
            ZLinkStreamConnectionState.CONNECTED,
            ZLinkStreamConnectionState.CLOSED)), "state change events mismatch");
        require(events.contains("Disconnected"), "disconnect handler not invoked");

        ZLinkStreamConnector reconnected = createConnector();
        List<ZLinkStreamConnectionState> reconnectStates = new ArrayList<>();
        reconnected.onConnectionStateChanged(state -> {
            reconnectStates.add(state);
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        });
        reconnected.connectAsync().toCompletableFuture().join();
        require(reconnected.isConnected(), "reconnect smoke failed");
        require(reconnectStates.equals(List.of(ZLinkStreamConnectionState.CONNECTED)),
            "reconnect state event mismatch");
        reconnected.close();
        System.out.println("StreamingClient sample self-check passed");
    }

    private static ZLinkStreamConnector createConnector() {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create("tcp://127.0.0.1:29200"),
            ZLinkStreamDispatchMode.MANUAL,
            Duration.ofSeconds(3),
            2));
    }

    private static ZLinkStreamEncodedPayload payload(
        String packetName,
        String payload,
        Map<String, String> metadata) {
        return new ZLinkStreamEncodedPayload(packetName, Message.from(payload), metadata);
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
