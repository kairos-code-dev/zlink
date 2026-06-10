package systems.zlink.samples.kotlin.tictactoe.server.api.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.AuthenticatePlayerRes

@ZLinkHandlerGroup("api")
class AuthenticatePlayerHandler {
    @ZLinkRequest(packetName = "AuthenticatePlayerReq")
    suspend fun handle(request: AuthenticatePlayerReq): AuthenticatePlayerRes {
        val actorId = request.accessToken.trim()
        require(actorId.isNotBlank()) { "authentication token is empty" }
        return AuthenticatePlayerRes(actorId)
    }
}
