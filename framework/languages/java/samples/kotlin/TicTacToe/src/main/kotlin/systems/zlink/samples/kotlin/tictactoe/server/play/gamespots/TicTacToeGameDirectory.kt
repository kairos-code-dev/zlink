package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots

object TicTacToeGameDirectory {
    private val games = linkedMapOf<String, TicTacToeGame>()

    fun create(gameName: String): TicTacToeGame {
        val gameId = "room-${games.size + 1}"
        return TicTacToeGame(gameId, gameName).also { games[gameId] = it }
    }

    fun get(gameId: String): TicTacToeGame =
        games[gameId] ?: throw IllegalArgumentException("unknown game $gameId")
}
