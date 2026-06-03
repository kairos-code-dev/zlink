package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorJoin
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.TicTacToeGameJoinReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.TicTacToeGameJoinRes

@ZLinkHandlerGroup("play-actor")
class TicTacToeGameJoinHandler {
    @ZLinkSpotActorJoin
    fun join(
        actor: PlayActor,
        request: TicTacToeGameJoinReq,
    ): TicTacToeGameJoinRes =
        actor.context()
            .getSpot(TicTacToeGame::class.java)
            .join(request.actorId)
}
