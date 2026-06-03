package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorRequest
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameDirectory
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlaceMarkRes

@ZLinkHandlerGroup("play-actor")
class PlayActorPlaceMarkHandler {
    fun placeMark(gameId: String, actorId: String, cell: Int): PlaceMarkRes =
        TicTacToeGameDirectory.get(gameId).placeMark(actorId, cell)

    @ZLinkSpotActorRequest
    fun placeMark(
        actor: PlayActor,
        request: PlaceMarkReq,
    ): PlaceMarkRes =
        TicTacToeGameDirectory.get(actor.gameId)
            .placeMark(actor.actorId(), request.cell)
}
