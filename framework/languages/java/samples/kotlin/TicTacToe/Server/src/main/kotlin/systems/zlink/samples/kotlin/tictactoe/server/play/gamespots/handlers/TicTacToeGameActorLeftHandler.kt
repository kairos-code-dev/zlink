package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkSpotActorLeft
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame

class TicTacToeGameActorLeftHandler {
    @ZLinkSpotActorLeft
    suspend fun actorLeft(
        spot: TicTacToeGame,
        actor: PlayActor,
        info: ZLinkSpotActorChangeResult,
        cancellationToken: CancellationToken,
    ) {
    }
}
