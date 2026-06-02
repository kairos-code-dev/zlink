package systems.zlink.samples.kotlin.tictactoe.server.api.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.samples.kotlin.tictactoe.server.play.handlers.CreateGameHandler

class CreateGameHttpHandler : ZLinkRequestHandler<String, String> {
    private val playHandler = CreateGameHandler()

    override fun handleAsync(request: String, context: ZLinkRequestContext): CompletionStage<String> =
        playHandler.createAsync(request)
}
