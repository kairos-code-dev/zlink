package systems.zlink.samples.kotlin.tictactoe.server.play.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameDirectory
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameRes

@ZLinkHandlerGroup("play")
class CreateGameHandler {
    @ZLinkRequest(packetName = "CreateGameReq")
    fun createAsync(gameName: String): CompletionStage<CreateGameRes> =
        CompletableFuture.completedFuture(
            CreateGameRes(
                gameId = TicTacToeGameDirectory.create(gameName).gameId,
                playEndpoint = SampleTopology.PlayStreamEndpoint,
                gameName = gameName,
            ),
        )
}
