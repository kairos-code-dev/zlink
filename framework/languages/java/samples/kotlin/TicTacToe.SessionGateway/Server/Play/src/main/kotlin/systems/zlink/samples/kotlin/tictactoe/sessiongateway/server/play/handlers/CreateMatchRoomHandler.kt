package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory

@ZLinkHandlerGroup("play")
class CreateMatchRoomHandler(
    private val games: TicTacToeGameDirectory,
) {
    @ZLinkRequest(packetName = "CreateMatchRoom")
    suspend fun handle(
        ownerActorId: String,
    ): String {
        val room = games.create(ownerActorId)
        return "${room.matchId}|$ownerActorId"
    }
}
