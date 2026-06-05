package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.GameNotificationPublisher
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory

@ZLinkHandlerGroup("play")
class JoinMatchHandler(
    private val games: TicTacToeGameDirectory,
) {
    @ZLinkRequest(packetName = "JoinMatchReq")
    fun handleAsync(
        request: String,
    ): CompletionStage<String> {
        val parts = request.split("|", limit = 2)
        if (parts.size < 2) {
            return CompletableFuture.failedFuture(
                IllegalArgumentException("JoinMatchReq payload must contain matchId and actorId"),
            )
        }
        return CompletableFuture.completedFuture(
            GameNotificationPublisher.encode(games.get(parts[0]).join(parts[1])),
        )
    }
}
