package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.api.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest

@ZLinkHandlerGroup("api")
class CreateMatchHandler {
    @ZLinkRequest(packetName = "CreateMatch")
    fun handleAsync(request: String): CompletionStage<String> =
        CompletableFuture.completedFuture("match-$request")
}
