package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot.TicTacToeEntrySpot
import systems.zlink.samples.kotlin.tictactoe.sessiongateway.shared.actors.PlayerActor

class TicTacToeEntrySpotActorJoinedHandler {
    @ZLinkSpotPostActorJoined
    fun actorJoined(
        entrySpot: TicTacToeEntrySpot,
        actor: PlayerActor,
        info: ZLinkSpotActorChangeResult,
        cancellationToken: CancellationToken,
    ): CompletionStage<Void> = CompletableFuture.completedFuture(null)
}
