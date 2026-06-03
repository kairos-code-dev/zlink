package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots

object TicTacToeGameDirectory {
    private val games = linkedMapOf<String, TicTacToeGame>()

    fun register(game: TicTacToeGame) {
        games[game.gameId] = game
    }

    fun get(gameId: String): TicTacToeGame =
        games[gameId] ?: throw IllegalArgumentException("unknown game $gameId")

    fun findByActor(actorId: String): TicTacToeGame =
        games.values.firstOrNull { it.hasPlayer(actorId) }
            ?: throw IllegalArgumentException("actor has not joined a game: $actorId")
}
