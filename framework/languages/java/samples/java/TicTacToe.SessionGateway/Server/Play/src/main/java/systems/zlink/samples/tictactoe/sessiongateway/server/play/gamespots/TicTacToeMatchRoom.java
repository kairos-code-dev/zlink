package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots;

public record TicTacToeMatchRoom(String matchId, String lastActorId) {
    public TicTacToeMatchRoom(String matchId) {
        this(matchId, "");
    }
}
