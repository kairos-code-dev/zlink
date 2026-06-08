package systems.zlink.samples.bingo.server.play.bingoroomspots;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoNotificationPublisher {
    public CompletionStage<Void> publishAsync(List<BingoRoomModels.RoomEvent> events) {
        CompletionStage<Void> stage = CompletableFuture.completedFuture(null);
        for (BingoRoomModels.RoomEvent event : events) {
            stage = stage.thenCompose(ignored -> publishAsync(event));
        }
        return stage;
    }

    private CompletionStage<Void> publishAsync(BingoRoomModels.RoomEvent event) {
        return switch (event.kind()) {
            case PLAYER_JOINED -> event.recipient().context().boundSession()
                .send(new Messages.PlayerJoinedNotify(
                    event.state().roomId(),
                    event.joinedActorId(),
                    event.joinedDisplayName(),
                    event.seat(),
                    event.host(),
                    event.state()))
                .submitAsync();
            case GAME_STARTED -> event.recipient().context().boundSession()
                .send(new Messages.BingoGameStartedNotify(event.state()))
                .submitAsync();
            case NUMBER_DRAWN -> event.recipient().context().boundSession()
                .send(new Messages.BingoNumberDrawnNotify(
                    event.state().roomId(),
                    event.state().drawSeq(),
                    event.drawnNumber(),
                    event.state()))
                .submitAsync();
            case STATE -> event.recipient().context().boundSession()
                .send(new Messages.BingoStateNotify(event.state()))
                .submitAsync();
            case GAME_ENDED -> event.recipient().context().boundSession()
                .send(new Messages.BingoGameEndedNotify(event.state()))
                .submitAsync();
        };
    }
}
