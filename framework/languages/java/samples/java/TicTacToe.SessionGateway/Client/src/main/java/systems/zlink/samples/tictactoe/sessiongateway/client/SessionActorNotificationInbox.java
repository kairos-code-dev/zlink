package systems.zlink.samples.tictactoe.sessiongateway.client;

import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CompletableFuture;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class SessionActorNotificationInbox {
    private final CopyOnWriteArrayList<String> turnChanged = new CopyOnWriteArrayList<>();
    private final CopyOnWriteArrayList<String> opponentJoined = new CopyOnWriteArrayList<>();
    private final CopyOnWriteArrayList<String> gameEnded = new CopyOnWriteArrayList<>();

    public void register(ZLinkStreamConnector connector) {
        connector.on(SampleNames.TurnChangedPacket, message -> {
            turnChanged.add(message.payload().payload().toUtf8String());
            return CompletableFuture.completedFuture(null);
        });
        connector.on(SampleNames.OpponentJoinedPacket, message -> {
            opponentJoined.add(message.payload().payload().toUtf8String());
            return CompletableFuture.completedFuture(null);
        });
        connector.on(SampleNames.GameEndedPacket, message -> {
            gameEnded.add(message.payload().payload().toUtf8String());
            return CompletableFuture.completedFuture(null);
        });
    }

    public List<String> turnChanged() {
        return List.copyOf(turnChanged);
    }

    public List<String> opponentJoined() {
        return List.copyOf(opponentJoined);
    }

    public List<String> gameEnded() {
        return List.copyOf(gameEnded);
    }
}
