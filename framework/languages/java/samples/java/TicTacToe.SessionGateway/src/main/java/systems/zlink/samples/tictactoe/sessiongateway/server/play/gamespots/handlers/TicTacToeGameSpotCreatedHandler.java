package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.handlers;

import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameModels;

public final class TicTacToeGameSpotCreatedHandler {
    public TicTacToeGameModels.State handle(String matchId) {
        return new TicTacToeGameModels.State(matchId, "created");
    }
}
