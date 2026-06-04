package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots;

public final class TicTacToeGameModels {
    private TicTacToeGameModels() {
    }

    public record State(String matchId, String value) {
    }
}
