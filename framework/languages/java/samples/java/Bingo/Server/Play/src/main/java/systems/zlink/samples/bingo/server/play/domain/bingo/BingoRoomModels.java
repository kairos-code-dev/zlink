package systems.zlink.samples.bingo.server.play.domain.bingo;

import java.util.List;
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
        String recipientActorId,
        Messages.BingoRoomState state,
        String joinedActorId,
        String joinedDisplayName,
        int seat,
        boolean host,
        int drawnNumber) {
    }

    public record RoomPlayer(
        String actorId,
        String displayName,
        int seat,
        BingoCard card) {
        public Messages.BingoPlayerState toState(String hostActorId) {
            return new Messages.BingoPlayerState(
                actorId,
                displayName,
                seat,
                actorId.equals(hostActorId),
                card == null ? List.of() : card.numbersSnapshot(),
                card == null ? List.of() : card.marksSnapshot(),
                card == null ? 0 : card.completedLines());
        }
    }

    public record BingoRoomSettings(
        String roomName,
        String mode,
        int requiredPlayers,
        int maxDrawNumber,
        long drawPeriodMillis) {
        public static BingoRoomSettings create(String mode, int roomSeq) {
            if (!"two-player".equals(mode)) {
                throw new IllegalStateException("Unsupported bingo mode. mode=" + mode);
            }
            return new BingoRoomSettings(
                "Bingo Room %03d".formatted(roomSeq),
                mode,
                2,
                15,
                SampleTimings.DrawPeriod.toMillis());
        }
    }
}
