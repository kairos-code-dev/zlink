package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.api.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest

@ZLinkHandlerGroup("api")
class AuthenticateActorHandler {
    @ZLinkRequest(packetName = "AuthenticateActor")
    fun handleAsync(request: String): CompletionStage<String> {
        require(request.startsWith("player-")) { "unknown actor token" }
        return CompletableFuture.completedFuture(request)
    }
}
