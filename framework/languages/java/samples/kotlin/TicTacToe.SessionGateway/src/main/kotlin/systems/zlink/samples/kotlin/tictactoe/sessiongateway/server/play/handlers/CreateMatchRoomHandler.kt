package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.handlers

class CreateMatchRoomHandler {
    fun create(actorId: String): String = "match-$actorId"
}
