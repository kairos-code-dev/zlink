package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorJoin
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes

@ZLinkHandlerGroup("play-actor")
class TicTacToeGameJoinHandler {
    @ZLinkSpotActorJoin
    fun join(
        actor: PlayActor,
        request: JoinGameReq,
    ): JoinGameRes =
        actor.context()
            .getSpot(TicTacToeGame::class.java)
            .join(actor.actorId())
}
