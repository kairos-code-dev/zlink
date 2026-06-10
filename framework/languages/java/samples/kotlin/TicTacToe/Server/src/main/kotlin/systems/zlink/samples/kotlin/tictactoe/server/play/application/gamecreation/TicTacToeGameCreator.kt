package systems.zlink.samples.kotlin.tictactoe.server.play.application.gamecreation

import java.util.concurrent.atomic.AtomicInteger

class TicTacToeGameCreator {
    private val sequence = AtomicInteger()

    fun nextRoom(gameName: String?): GameRoom {
        val normalized = gameName?.takeIf { it.isNotBlank() } ?: "tic-tac-toe"
        return GameRoom("ttt-room-%03d".format(sequence.incrementAndGet()), normalized)
    }

    data class GameRoom(val roomId: String, val gameName: String)
}
