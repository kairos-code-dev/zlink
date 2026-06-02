package systems.zlink.samples.kotlin.tictactoe.server.play.actors

class PlayActor(
    val actorId: String,
) {
    var gameId: String = ""
        private set

    fun joinGame(gameId: String) {
        this.gameId = gameId
    }
}
