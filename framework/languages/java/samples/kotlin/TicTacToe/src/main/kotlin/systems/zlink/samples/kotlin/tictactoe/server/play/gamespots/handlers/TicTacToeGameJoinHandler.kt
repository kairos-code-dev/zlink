package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorJoin
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGameDirectory
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.JoinGameRes

@ZLinkHandlerGroup("play-actor")
class TicTacToeGameJoinHandler {
    fun join(gameId: String, actorId: String): JoinGameRes =
        TicTacToeGameDirectory.get(gameId).join(actorId)

    @ZLinkSpotActorJoin
    fun join(
        actor: PlayActor,
        request: JoinGameReq,
    ): JoinGameRes {
        val reply = TicTacToeGameDirectory.get(request.gameId).join(actor.actorId())
        actor.joinGame(request.gameId)
        return reply
    }
}
