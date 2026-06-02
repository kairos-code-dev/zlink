package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session.sessions.handlers

class AuthenticateSessionPacketHandler {
    fun authenticate(token: String): String = token
}
