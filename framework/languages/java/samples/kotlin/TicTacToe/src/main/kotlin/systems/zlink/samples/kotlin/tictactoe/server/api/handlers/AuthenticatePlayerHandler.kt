package systems.zlink.samples.kotlin.tictactoe.server.api.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest

@ZLinkHandlerGroup("api")
class AuthenticatePlayerHandler {
    @ZLinkRequest(packetName = "AuthenticatePlayer")
    fun handleAsync(request: String): CompletionStage<String> {
        val actorId = when (request) {
            "alice-token" -> "alice"
            "bob-token" -> "bob"
            else -> throw IllegalArgumentException("unknown access token")
        }
        return CompletableFuture.completedFuture(actorId)
    }
}
