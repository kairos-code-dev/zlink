package systems.zlink.samples.kotlin.tictactoe.server.play.handlers

import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameRes

@ZLinkHandlerGroup(SampleNames.PlayChannel)
class CreateGameHandler(
    private val spots: ZLinkSpotManager,
    private val settings: SampleSettings,
) {
    @ZLinkRequest(packetName = "CreateGameReq")
    fun createAsync(request: CreateGameReq): CompletionStage<CreateGameRes> =
        spots.createAsync(TicTacToeGame::class.java)
            .thenApply { created ->
                CreateGameRes(
                    gameId = created.spotRid().toHex(),
                    playEndpoint = settings.playEndpoint,
                    gameName = request.gameName,
                )
            }
}
