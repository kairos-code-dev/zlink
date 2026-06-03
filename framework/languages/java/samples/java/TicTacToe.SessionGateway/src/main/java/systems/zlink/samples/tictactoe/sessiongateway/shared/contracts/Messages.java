package systems.zlink.samples.tictactoe.sessiongateway.shared.contracts;

public final class Messages {
    private Messages() {
    }

    public record AuthenticateActorReq(String accessToken) {
    }

    public record AuthenticateActorRes(String actorId) {
    }

    public record CreateMatchReq(String ownerActorId) {
    }

    public record CreateMatchRes(String matchId, String ownerActorId) {
    }

    public record JoinMatchReq(String matchId) {
    }

    public record JoinMatchRes(
        String matchId,
        String actorId,
        String mark,
        TicTacToeState state) {
    }

    public record PlaceMarkReq(String matchId, int cell) {
    }

    public record PlaceMarkRes(TicTacToeState state) {
    }

    public record OpponentJoinedNotify(
        String matchId,
        String opponentActorId,
        String mark,
        TicTacToeState state) {
    }

    public record TurnChangedNotify(
        String matchId,
        String turnActorId,
        TicTacToeState state) {
    }

    public record GameEndedNotify(
        String matchId,
        String winnerActorId,
        boolean draw,
        TicTacToeState state) {
    }

    public record GameStateChanged(String state) {
    }

    public record TicTacToeState(
        String matchId,
        String board,
        String status,
        String turnActorId,
        String winnerActorId,
        boolean draw,
        String xActorId,
        String oActorId,
        String lastMoveActorId,
        Integer lastMoveCell) {
    }
}
