package systems.zlink.samples.kotlin.bingo.server.api.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.AuthenticatePlayerRes

@ZLinkHandlerGroup("api")
class AuthenticatePlayerHandler(
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<AuthenticatePlayerReq, AuthenticatePlayerRes> {
    override fun handle(
        request: AuthenticatePlayerReq,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        AuthenticatePlayerRes(
            accepted = true,
            actorId = request.accessToken,
            displayName = request.accessToken,
            reason = null,
        )
    }
}
