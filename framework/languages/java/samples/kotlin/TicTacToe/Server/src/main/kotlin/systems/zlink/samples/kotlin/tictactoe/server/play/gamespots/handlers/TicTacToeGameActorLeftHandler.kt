package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkSpotActorLeft
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.TicTacToeGame

class TicTacToeGameActorLeftHandler {
    @ZLinkSpotActorLeft
    fun actorLeft(
        spot: TicTacToeGame,
        actor: PlayActor,
        info: ZLinkSpotActorChangeResult,
        cancellationToken: CancellationToken,
    ): CompletionStage<Void> = CompletableFuture.completedFuture(null)
}
