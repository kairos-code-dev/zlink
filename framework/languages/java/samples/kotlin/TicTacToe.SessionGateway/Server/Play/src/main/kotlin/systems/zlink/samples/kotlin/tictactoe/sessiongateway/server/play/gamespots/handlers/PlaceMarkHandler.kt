package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.GameNotificationPublisher

@ZLinkHandlerGroup("play")
class PlaceMarkHandler {
    @ZLinkRequest(packetName = "PlaceMarkReq")
    fun handleAsync(
        request: String,
    ): CompletionStage<String> {
        val parts = request.split("|", limit = 3)
        return CompletableFuture.completedFuture(
            GameNotificationPublisher.encode(
                TicTacToeGameDirectory.get(parts[0]).placeMark(parts[2], parts[1].toInt()),
            ),
        )
    }
}
