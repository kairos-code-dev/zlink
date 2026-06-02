package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.api.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler

class AuthenticateActorHandler : ZLinkRequestHandler<String, String> {
    override fun handleAsync(request: String, context: ZLinkRequestContext): CompletionStage<String> {
        require(request.startsWith("player-")) { "unknown actor token" }
        return CompletableFuture.completedFuture(request)
    }
}
