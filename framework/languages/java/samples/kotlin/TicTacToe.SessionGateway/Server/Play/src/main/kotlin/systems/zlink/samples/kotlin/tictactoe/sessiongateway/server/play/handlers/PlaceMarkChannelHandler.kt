package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.GameNotificationPublisher
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory

@ZLinkHandlerGroup("play")
class PlaceMarkChannelHandler(
    private val games: TicTacToeGameDirectory,
) {
    @ZLinkRequest(packetName = "PlaceMarkReq")
    suspend fun handle(
        request: String,
    ): String {
        val parts = request.split("|", limit = 3)
        if (parts.size < 3) {
            throw IllegalArgumentException("PlaceMarkReq payload must contain matchId, cell, and actorId")
        }
        return GameNotificationPublisher.encode(
            games.get(parts[0]).placeMark(parts[2], parts[1].toInt()),
        )
    }
}
