package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameCatalog
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes

class TicTacToeGameJoinHandler {
    fun join(gameId: String, actorId: String): JoinGameRes =
        TicTacToeGameCatalog.get(gameId).join(actorId)
}
