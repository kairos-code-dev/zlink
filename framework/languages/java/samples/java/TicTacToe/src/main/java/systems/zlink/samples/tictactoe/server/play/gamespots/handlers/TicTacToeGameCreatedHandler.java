package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;

public final class TicTacToeGameCreatedHandler {
    public String created(TicTacToeGame game) {
        return game.gameId();
    }
}
