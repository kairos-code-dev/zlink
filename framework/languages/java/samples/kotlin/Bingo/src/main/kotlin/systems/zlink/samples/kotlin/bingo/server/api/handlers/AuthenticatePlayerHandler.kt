package systems.zlink.samples.kotlin.bingo.server.api.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerRes

class AuthenticatePlayerHandler : ZLinkRequestHandler<AuthenticatePlayerReq, AuthenticatePlayerRes> {
    override fun handleAsync(
        request: AuthenticatePlayerReq,
        context: ZLinkRequestContext,
    ): CompletionStage<AuthenticatePlayerRes> =
        CompletableFuture.completedFuture(
            AuthenticatePlayerRes(
                accepted = true,
                actorId = request.accessToken,
                displayName = request.accessToken,
                reason = null,
            ),
        )
}
