package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.tictactoegamespot.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes

@ZLinkHandlerGroup("play-actor")
class PlayActorPlaceMarkHandler {
    @ZLinkSpotActorRequest
    suspend fun placeMark(
        spot: TicTacToeGame,
        actor: PlayActor,
        context: ZLinkSpotActorRequestContext,
        request: PlaceMarkReq,
    ): PlaceMarkRes {
        actor.requireJoinedGame()
        return spot.placeMark(actor, request.cell)
    }
}
