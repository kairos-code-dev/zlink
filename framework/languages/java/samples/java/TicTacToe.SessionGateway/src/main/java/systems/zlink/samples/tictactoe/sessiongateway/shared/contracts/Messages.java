package systems.zlink.samples.tictactoe.sessiongateway.shared.contracts;

public final class Messages {
    private Messages() {
    }

    public record AuthenticateActorReq(String accessToken) {
    }

    public record AuthenticateActorRes(String actorId) {
    }

    public record CreateMatchReq(String actorId) {
    }

    public record CreateMatchRes(String matchId) {
    }

    public record GameStateChanged(String state) {
    }
}
