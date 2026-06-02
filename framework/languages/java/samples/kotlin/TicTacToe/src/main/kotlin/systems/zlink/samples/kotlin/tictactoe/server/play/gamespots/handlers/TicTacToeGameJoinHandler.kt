package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameDirectory
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes

class TicTacToeGameJoinHandler {
    fun join(gameId: String, actorId: String): JoinGameRes =
        TicTacToeGameDirectory.get(gameId).join(actorId)
}
