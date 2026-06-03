package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots

import java.util.concurrent.ConcurrentHashMap

object TicTacToeGameDirectory {
    private val rooms = ConcurrentHashMap<String, TicTacToeGameSpot>()

    fun create(ownerActorId: String): TicTacToeGameSpot {
        val matchId = "match-$ownerActorId"
        val room = TicTacToeGameSpot(matchId, ownerActorId)
        rooms[matchId] = room
        return room
    }

    fun get(matchId: String): TicTacToeGameSpot =
        rooms[matchId] ?: throw IllegalArgumentException("Unknown match: $matchId")
}
