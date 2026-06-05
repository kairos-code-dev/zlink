package systems.zlink.samples.bingo.server.play.bingoroomspots;

import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.shared.configuration.SampleTimings;
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

    public record BingoRoomSettings(
        String roomName,
        String mode,
        int requiredPlayers,
        int maxDrawNumber,
        long drawPeriodMillis) {
        public static BingoRoomSettings create(String mode, int roomSeq) {
            if (!"four-player".equals(mode)) {
                throw new IllegalStateException("Unsupported bingo mode. mode=" + mode);
            }
            return new BingoRoomSettings(
                "Bingo Room %03d".formatted(roomSeq),
                mode,
                4,
                75,
                SampleTimings.DrawPeriod.toMillis());
        }
    }
}
