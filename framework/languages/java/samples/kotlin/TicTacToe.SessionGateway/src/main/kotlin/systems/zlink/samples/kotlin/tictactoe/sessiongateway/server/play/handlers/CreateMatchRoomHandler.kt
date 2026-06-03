package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory

class CreateMatchRoomHandler : ZLinkRequestHandler<String, String> {
    override fun handleAsync(
        ownerActorId: String,
        context: ZLinkRequestContext,
    ): CompletionStage<String> {
        val room = TicTacToeGameDirectory.create(ownerActorId)
        return CompletableFuture.completedFuture("${room.matchId}|${room.ownerActorId}")
    }
}
