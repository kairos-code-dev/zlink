package systems.zlink.samples.kotlin.tictactoe.server.api.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerRes

@ZLinkHandlerGroup("api")
class AuthenticatePlayerHandler {
    @ZLinkRequest(packetName = "AuthenticatePlayer")
    fun handleAsync(request: AuthenticatePlayerReq): CompletionStage<AuthenticatePlayerRes> {
        val actorId = when (request.accessToken) {
            "alice-token" -> "alice"
            "bob-token" -> "bob"
            else -> throw IllegalArgumentException("unknown access token")
        }
        return CompletableFuture.completedFuture(AuthenticatePlayerRes(actorId))
    }
}
