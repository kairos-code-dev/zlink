package systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.handlers

import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameCatalog
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes

class PlayActorJoinGameHandler {
    fun joinGame(gameId: String, actorId: String): JoinGameRes =
        TicTacToeGameCatalog.get(gameId).join(actorId)
}
