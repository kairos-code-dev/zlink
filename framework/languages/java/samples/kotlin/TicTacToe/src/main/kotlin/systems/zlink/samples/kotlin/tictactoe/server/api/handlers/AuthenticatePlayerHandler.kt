package systems.zlink.samples.kotlin.tictactoe.server.api.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler

class AuthenticatePlayerHandler : ZLinkRequestHandler<String, String> {
    override fun handleAsync(request: String, context: ZLinkRequestContext): CompletionStage<String> {
        val actorId = when (request) {
            "alice-token" -> "alice"
            "bob-token" -> "bob"
            else -> throw IllegalArgumentException("unknown access token")
        }
        return CompletableFuture.completedFuture(actorId)
    }
}
