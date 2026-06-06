package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.api.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest

@ZLinkHandlerGroup("api")
class AuthenticateActorHandler {
    @ZLinkRequest(packetName = "AuthenticateActor")
    suspend fun handle(request: String): String {
        require(request.startsWith("player-")) { "unknown actor token" }
        return request
    }
}
