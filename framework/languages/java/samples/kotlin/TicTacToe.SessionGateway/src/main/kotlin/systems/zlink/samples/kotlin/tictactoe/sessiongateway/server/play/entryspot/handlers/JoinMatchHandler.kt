package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory

class JoinMatchHandler : ZLinkRequestHandler<String, String> {
    override fun handleAsync(
        request: String,
        context: ZLinkRequestContext,
    ): CompletionStage<String> {
        val parts = request.split("|", limit = 2)
        return CompletableFuture.completedFuture(
            TicTacToeGameDirectory.get(parts[0]).join(parts[1]).encode(),
        )
    }
}
