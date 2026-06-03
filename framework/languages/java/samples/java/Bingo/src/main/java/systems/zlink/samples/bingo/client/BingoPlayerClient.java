package systems.zlink.samples.bingo.client;

import java.net.URI;
import java.time.Duration;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoWinnerSink;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.ZLinkStreamEncodedPayload;

public final class BingoPlayerClient implements BingoWinnerSink, AutoCloseable {
    private final String playerId;
    private final ZLinkStreamConnector connector;
    private final BingoNotificationInbox inbox = new BingoNotificationInbox();

    public BingoPlayerClient(String playerId) {
        this.playerId = playerId;
        this.connector = ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(SampleTopology.StreamEndpoint + "/" + playerId),
            ZLinkStreamDispatchMode.MANUAL,
            Duration.ofSeconds(3),
            2));
    }

    public CompletionStage<Void> connectAsync() {
        connector.on("BingoWinner", message -> {
            inbox.add(message.payload().payload().toUtf8String());
            return CompletableFuture.completedFuture(null);
        });
        return connector.connectAsync();
    }

    public CompletionStage<String> authenticateAsync() {
        return connector.request(new ZLinkStreamEncodedPayload(
                "AuthenticateReq",
                Message.from(playerId),
                Map.of()))
            .packetName("AuthenticateReq")
            .submitAsync()
            .thenApply(reply -> reply.payload().toUtf8String());
    }

    public CompletionStage<Void> pushAsync(String packetName, String payload, String roomId) {
        return connector.send(new ZLinkStreamEncodedPayload(
                packetName,
                Message.from(payload),
                Map.of("roomId", roomId)))
            .packetName(packetName)
            .metadata("playerId", playerId)
            .submitAsync();
    }

    @Override
    public CompletionStage<Void> publishWinnerAsync(String payload, String roomId) {
        return pushAsync("BingoWinner", payload, roomId);
    }

    public CompletionStage<Void> dispatchAsync() {
        return connector.dispatchAsync();
    }

    @Override
    public String playerId() {
        return playerId;
    }

    public BingoNotificationInbox inbox() {
        return inbox;
    }

    @Override
    public void close() {
        connector.close();
    }
}
