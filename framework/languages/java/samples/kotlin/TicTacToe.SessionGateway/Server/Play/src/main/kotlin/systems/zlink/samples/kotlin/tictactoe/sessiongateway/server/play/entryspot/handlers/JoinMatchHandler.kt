package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.GameNotificationPublisher
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory

@ZLinkHandlerGroup("play")
class JoinMatchHandler(
    private val games: TicTacToeGameDirectory,
) {
    @ZLinkRequest(packetName = "JoinMatchReq")
    suspend fun handle(
        request: String,
    ): String {
        val parts = request.split("|", limit = 2)
        if (parts.size < 2) {
            throw IllegalArgumentException("JoinMatchReq payload must contain matchId and actorId")
        }
        return GameNotificationPublisher.encode(games.get(parts[0]).join(parts[1]))
    }
}
