package systems.zlink.samples.kotlin.bingo.server.api.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerRes

@ZLinkHandlerGroup("api")
class AuthenticatePlayerHandler {
    @ZLinkRequest(packetName = "AuthenticatePlayer")
    fun handleAsync(request: AuthenticatePlayerReq): CompletionStage<AuthenticatePlayerRes> =
        CompletableFuture.completedFuture(
            AuthenticatePlayerRes(
                accepted = true,
                actorId = request.accessToken,
                displayName = request.accessToken,
                reason = null,
            ),
        )
}
