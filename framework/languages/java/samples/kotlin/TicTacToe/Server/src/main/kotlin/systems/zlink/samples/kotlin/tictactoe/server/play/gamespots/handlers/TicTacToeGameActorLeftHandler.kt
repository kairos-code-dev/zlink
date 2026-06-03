package systems.zlink.samples.kotlin.tictactoe.server.play.gamespots.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkSpotActorLeft
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor

class TicTacToeGameActorLeftHandler {
    @ZLinkSpotActorLeft
    fun actorLeft(
        actor: PlayActor,
        info: ZLinkSpotActorChangeResult,
    ): CompletionStage<Void> = CompletableFuture.completedFuture(null)
}
