package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.api.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest

@ZLinkHandlerGroup("api")
class CreateMatchHandler {
    @ZLinkRequest(packetName = "CreateMatch")
    suspend fun handle(request: String): String =
        "match-$request"
}
