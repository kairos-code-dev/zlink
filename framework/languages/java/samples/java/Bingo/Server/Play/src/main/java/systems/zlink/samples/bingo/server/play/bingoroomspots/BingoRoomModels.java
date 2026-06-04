package systems.zlink.samples.bingo.server.play.bingoroomspots;

import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoRoomModels {
    private BingoRoomModels() {
    }

    public enum EventKind {
        PLAYER_JOINED,
        GAME_STARTED,
        NUMBER_DRAWN,
        STATE,
        GAME_ENDED
    }

    public record RoomEvent(
        EventKind kind,
        PlayerActor recipient,
        Messages.BingoRoomState state,
        String joinedActorId,
        String joinedDisplayName,
        int seat,
        boolean host,
        int drawnNumber) {
    }

    public record RoomPlayer(PlayerActor actor, int seat, BingoCard card) {
        public Messages.BingoPlayerState toState(String hostActorId) {
            return new Messages.BingoPlayerState(
                actor.actorId(),
                actor.displayName(),
                seat,
                actor.actorId().equals(hostActorId),
                card.numbersSnapshot(),
                card.marksSnapshot(),
                card.completedLines());
        }
    }
}
