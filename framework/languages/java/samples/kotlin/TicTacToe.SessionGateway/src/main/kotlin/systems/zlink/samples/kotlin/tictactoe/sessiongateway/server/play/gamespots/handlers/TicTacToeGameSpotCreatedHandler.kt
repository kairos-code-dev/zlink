package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.handlers

import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameModels

class TicTacToeGameSpotCreatedHandler {
    fun handle(matchId: String): TicTacToeGameModels.State =
        TicTacToeGameModels.State(matchId, "created")
}
