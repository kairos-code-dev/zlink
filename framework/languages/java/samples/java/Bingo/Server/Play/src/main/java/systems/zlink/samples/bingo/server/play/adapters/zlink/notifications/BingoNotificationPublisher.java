package systems.zlink.samples.bingo.server.play.adapters.zlink.notifications;

import java.util.List;
import java.util.function.Function;
import systems.zlink.samples.bingo.server.play.adapters.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoNotificationPublisher {
    public void publish(
        List<BingoRoomModels.RoomEvent> events,
        Function<String, PlayerActor> actorResolver) {
        for (BingoRoomModels.RoomEvent event : events) {
            publish(event, actorResolver.apply(event.recipientActorId()));
        }
    }

    private void publish(BingoRoomModels.RoomEvent event, PlayerActor recipient) {
        if (recipient == null) {
            return;
        }
        switch (event.kind()) {
            case PLAYER_JOINED -> recipient.context().boundSession()
                .send(new Messages.PlayerJoinedNotify(
                    event.state().roomId(),
                    event.joinedActorId(),
                    event.joinedDisplayName(),
                    event.seat(),
                    event.host(),
                    event.state()))
                .await();
            case GAME_STARTED -> recipient.context().boundSession()
                .send(new Messages.BingoGameStartedNotify(event.state()))
                .await();
            case NUMBER_DRAWN -> recipient.context().boundSession()
                .send(new Messages.BingoNumberDrawnNotify(
                    event.state().roomId(),
                    event.state().drawSeq(),
                    event.drawnNumber(),
                    event.state()))
                .await();
            case STATE -> recipient.context().boundSession()
                .send(new Messages.BingoStateNotify(event.state()))
                .await();
            case GAME_ENDED -> recipient.context().boundSession()
                .send(new Messages.BingoGameEndedNotify(event.state()))
                .await();
        }
    }
}
