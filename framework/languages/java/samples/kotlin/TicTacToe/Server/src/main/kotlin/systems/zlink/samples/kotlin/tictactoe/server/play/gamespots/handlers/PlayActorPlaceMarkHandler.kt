package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes

@ZLinkHandlerGroup("play-actor")
class PlayActorPlaceMarkHandler {
    @ZLinkSpotActorRequest
    fun placeMark(
        spot: TicTacToeGame,
        actor: PlayActor,
        context: ZLinkSpotActorRequestContext,
        request: PlaceMarkReq,
        cancellationToken: CancellationToken,
    ): java.util.concurrent.CompletionStage<PlaceMarkRes> {
        actor.requireJoinedGame()
        return spot.placeMark(actor, request.cell)
    }
}
