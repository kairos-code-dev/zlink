package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes

@ZLinkHandlerGroup("play-actor")
class PlayActorPlaceMarkHandler {
    @ZLinkSpotActorRequest
    fun placeMark(
        actor: PlayActor,
        request: PlaceMarkReq,
    ): java.util.concurrent.CompletionStage<PlaceMarkRes> {
        actor.requireJoinedGame()
        return actor.context()
            .getSpot(TicTacToeGame::class.java)
            .placeMark(actor, request.cell)
    }
}
