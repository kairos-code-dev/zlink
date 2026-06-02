package systems.zlink.samples.streamingclient;

import java.net.URI;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
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
        awaitSample(connector.connectAsync());
        require(connector.isConnected(), "connector did not connect");
        require(connector.options().dispatchMode() == ZLinkStreamDispatchMode.MANUAL,
            "connector did not keep manual dispatch option");

        awaitSample(connector.send(payload("GameInput", "move", Map.of("source", "send")))
            .packetName("GameInput")
            .submitAsync());
        require(connector.pendingDispatchCount() == 1, "manual dispatch did not queue send");
        awaitSample(connector.dispatchAsync());
        require(events.contains("GameInput:send"), "manual send handler not invoked");

        CompletionStage<ZLinkStreamEncodedPayload> replyStage = connector.request(payload(
                "GameInput",
                "query",
                Map.of("source", "request")))
            .packetName("GameInput")
            .metadata("requestId", "r1")
            .timeout(Duration.ofSeconds(2))
            .submitAsync();
        ZLinkStreamEncodedPayload reply = awaitSample(replyStage);
        require(reply.packetName().equals("GameInput"), "request packet name mismatch");
        awaitSample(connector.dispatchAsync());
        require(events.contains("GameInput:request"), "manual request handler not invoked");

        awaitSample(connector.closeAsync());
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
        awaitSample(reconnected.connectAsync());
        awaitSample(reconnected.disconnectAsync());
        awaitSample(reconnected.reconnectAsync());
        require(reconnected.isConnected(), "same connector reconnect smoke failed");
        require(reconnectStates.equals(List.of(
                ZLinkStreamConnectionState.CONNECTED,
                ZLinkStreamConnectionState.DISCONNECTED,
                ZLinkStreamConnectionState.RECONNECTING,
                ZLinkStreamConnectionState.CONNECTED)),
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

    private static <T> T awaitSample(CompletionStage<T> stage) throws Exception {
        CompletableFuture<T> done = new CompletableFuture<>();
        stage.whenComplete((value, error) -> {
            if (error != null) {
                done.completeExceptionally(error);
            } else {
                done.complete(value);
            }
        });
        return done.get();
    }
}
