package systems.zlink.samples.kotlin.tictactoe.server.play.handlers

import kotlinx.coroutines.future.await
import org.springframework.beans.factory.ObjectProvider
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
    private val spots: ObjectProvider<ZLinkSpotManager>,
    private val settings: SampleSettings,
) {
    @ZLinkRequest(packetName = "CreateGameReq")
    suspend fun create(request: CreateGameReq): CreateGameRes {
        val created = spots.getObject().createAsync(TicTacToeGame::class.java).await()
        return CreateGameRes(
            gameId = created.spotRid().toHex(),
            playEndpoint = settings.playEndpoint,
            gameName = request.gameName,
        )
    }
}
