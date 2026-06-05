package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory

@ZLinkHandlerGroup("play")
class CreateMatchRoomHandler(
    private val games: TicTacToeGameDirectory,
) {
    @ZLinkRequest(packetName = "CreateMatchRoom")
    fun handleAsync(
        ownerActorId: String,
    ): CompletionStage<String> {
        val room = games.create(ownerActorId)
        return CompletableFuture.completedFuture("${room.matchId}|$ownerActorId")
    }
}
