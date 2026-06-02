package systems.zlink.samples.bingo.server.play.bingoroomspots;

public final class BingoRoomModels {
    private BingoRoomModels() {
    }

    public record BingoWinner(String playerId) {
    }

    public record BingoRoomCreated(String roomId) {
    }

    public record BingoRoomMembership(String roomId, String actorId, boolean joined) {
    }
}
