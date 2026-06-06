package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.handlers.ZLinkSpotActorJoin
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.TicTacToeGameJoinReq
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.TicTacToeGameJoinRes

@ZLinkHandlerGroup("play-actor")
class TicTacToeGameJoinHandler {
    @ZLinkSpotActorJoin
    suspend fun join(
        spot: TicTacToeGame,
        actor: PlayActor,
        request: TicTacToeGameJoinReq,
        cancellationToken: CancellationToken,
    ): TicTacToeGameJoinRes =
        spot.join(actor, request.gameId)
}
