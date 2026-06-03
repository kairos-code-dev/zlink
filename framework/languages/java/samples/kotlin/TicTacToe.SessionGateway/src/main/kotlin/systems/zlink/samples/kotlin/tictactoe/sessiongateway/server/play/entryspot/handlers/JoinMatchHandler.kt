package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.GameNotificationPublisher

@ZLinkHandlerGroup("play")
class JoinMatchHandler {
    @ZLinkRequest(packetName = "JoinMatchReq")
    fun handleAsync(
        request: String,
    ): CompletionStage<String> {
        val parts = request.split("|", limit = 2)
        return CompletableFuture.completedFuture(
            GameNotificationPublisher.encode(TicTacToeGameDirectory.get(parts[0]).join(parts[1])),
        )
    }
}
