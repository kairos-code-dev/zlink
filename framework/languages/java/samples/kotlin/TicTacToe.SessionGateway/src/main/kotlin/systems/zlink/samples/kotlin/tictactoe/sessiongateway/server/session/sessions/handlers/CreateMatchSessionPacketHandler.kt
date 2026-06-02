package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers

class CreateMatchSessionPacketHandler {
    fun create(actorId: String): String = "match-$actorId"
}
