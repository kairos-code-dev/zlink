package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.handlers

import kotlinx.coroutines.future.await
import org.springframework.beans.factory.ObjectProvider
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkRequest
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleSettings
import systems.zlink.samples.kotlin.tictactoe.server.play.application.gamecreation.TicTacToeGameCreator
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.CreateGameRes

@ZLinkHandlerGroup(SampleNames.PlayHandlerGroup)
class CreateGameHandler(
    private val spots: ObjectProvider<ZLinkSpotManager>,
    private val settings: SampleSettings,
    private val gameCreator: TicTacToeGameCreator,
) : ZLinkSuspendingRequestHandler<CreateGameReq, CreateGameRes> {
    @ZLinkRequest
    override suspend fun handle(
        request: CreateGameReq,
        context: ZLinkRequestContext,
    ): CreateGameRes {
        val room = gameCreator.nextRoom(request.gameName)
        spots.getObject().create(TicTacToeGame::class.java, RoutingId.from(room.roomId)).await()
        return CreateGameRes(
            roomId = room.roomId,
            gameName = room.gameName,
            ownerPlayEndpoint = room.ownerPlayEndpoint,
            playEndpoints = room.playEndpoints,
            playNodes = room.playNodes,
            requiredLevel = room.requiredLevel,
        )
    }
}
