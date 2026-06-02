package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame

class TicTacToeGameCreatedHandler {
    fun created(game: TicTacToeGame): String = game.gameId
}
