package systems.zlink.samples.bingo.shared.contracts;

public final class Messages {
    private Messages() {
    }

    public record BingoWinner(String payload) {
    }

    public record AuthenticatePlayerReq(String accessToken) {
    }

    public record AuthenticatePlayerRes(boolean accepted, String actorId, String displayName, String reason) {
    }

    public record EnsurePlayerActorReq(String actorId, String displayName) {
    }

    public record EnsurePlayerActorRes(String actorId, String actorType) {
    }

    public record MatchBingoApiReq(String actorId, String displayName, String mode) {
    }

    public record MatchBingoApiRes(String roomId) {
    }

    public record AllocateBingoRoomReq(String mode) {
    }

    public record AllocateBingoRoomRes(String roomId) {
    }
}
