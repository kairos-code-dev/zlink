package systems.zlink.samples.kotlin.tictactoe.server.play.entryspot

import systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.handlers.PlayActorJoinGameHandler
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes

class PlayEntrySpot {
    private val joinGameHandler = PlayActorJoinGameHandler()

    fun joinGame(gameId: String, actorId: String): JoinGameRes =
        joinGameHandler.joinGame(gameId, actorId)
}
