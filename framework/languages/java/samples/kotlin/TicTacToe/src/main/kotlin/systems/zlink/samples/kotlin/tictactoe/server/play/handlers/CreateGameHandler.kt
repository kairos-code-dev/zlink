package systems.zlink.samples.kotlin.tictactoe.server.play.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameDirectory

@ZLinkHandlerGroup("play")
class CreateGameHandler {
    @ZLinkRequest(packetName = "CreateGameReq")
    fun createAsync(gameName: String): CompletionStage<String> =
        CompletableFuture.completedFuture(TicTacToeGameDirectory.create(gameName).gameId)
}
