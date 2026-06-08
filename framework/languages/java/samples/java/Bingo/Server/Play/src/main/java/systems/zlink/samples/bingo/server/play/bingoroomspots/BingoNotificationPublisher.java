package systems.zlink.samples.bingo.server.play.bingoroomspots;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoNotificationPublisher {
    public CompletionStage<Void> publishAsync(
        List<BingoRoomModels.RoomEvent> events,
        Function<String, PlayerActor> actorResolver) {
        CompletionStage<Void> stage = CompletableFuture.completedFuture(null);
        for (BingoRoomModels.RoomEvent event : events) {
            stage = stage.thenCompose(ignored -> publishAsync(event, actorResolver.apply(event.recipientActorId())));
        }
        return stage;
    }

    private CompletionStage<Void> publishAsync(BingoRoomModels.RoomEvent event, PlayerActor recipient) {
        if (recipient == null) {
            return CompletableFuture.completedFuture(null);
        }
        return switch (event.kind()) {
            case PLAYER_JOINED -> recipient.context().boundSession()
                .send(new Messages.PlayerJoinedNotify(
                    event.state().roomId(),
                    event.joinedActorId(),
                    event.joinedDisplayName(),
                    event.seat(),
                    event.host(),
                    event.state()))
                .submitAsync();
            case GAME_STARTED -> recipient.context().boundSession()
                .send(new Messages.BingoGameStartedNotify(event.state()))
                .submitAsync();
            case NUMBER_DRAWN -> recipient.context().boundSession()
                .send(new Messages.BingoNumberDrawnNotify(
                    event.state().roomId(),
                    event.state().drawSeq(),
                    event.drawnNumber(),
                    event.state()))
                .submitAsync();
            case STATE -> recipient.context().boundSession()
                .send(new Messages.BingoStateNotify(event.state()))
                .submitAsync();
            case GAME_ENDED -> recipient.context().boundSession()
                .send(new Messages.BingoGameEndedNotify(event.state()))
                .submitAsync();
        };
    }
}
