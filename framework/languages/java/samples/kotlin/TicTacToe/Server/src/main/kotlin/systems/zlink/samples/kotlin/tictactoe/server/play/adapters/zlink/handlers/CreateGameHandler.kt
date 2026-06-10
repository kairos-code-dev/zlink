package systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.handlers

import kotlinx.coroutines.future.await
import org.springframework.beans.factory.ObjectProvider
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.application.gamecreation.TicTacToeGameCreator
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameRes

@ZLinkHandlerGroup(SampleNames.PlayChannel)
class CreateGameHandler(
    private val spots: ObjectProvider<ZLinkSpotManager>,
    private val settings: SampleSettings,
    private val gameCreator: TicTacToeGameCreator,
) {
    @ZLinkRequest(packetName = "CreateGameReq")
    suspend fun create(request: CreateGameReq): CreateGameRes {
        val room = gameCreator.nextRoom(request.gameName)
        spots.getObject().createAsync(TicTacToeGame::class.java, RoutingId.from(room.roomId)).await()
        return CreateGameRes(
            roomId = room.roomId,
            playEndpoint = settings.playEndpoint,
            gameName = room.gameName,
        )
    }
}
