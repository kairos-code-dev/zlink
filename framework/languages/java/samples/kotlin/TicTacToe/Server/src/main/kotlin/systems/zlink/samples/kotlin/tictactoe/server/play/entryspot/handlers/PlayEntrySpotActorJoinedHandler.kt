package systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult
import systems.zlink.samples.kotlin.tictactoe.server.play.actors.PlayActor
import systems.zlink.samples.kotlin.tictactoe.server.play.entryspot.PlayEntrySpot

class PlayEntrySpotActorJoinedHandler {
    @ZLinkSpotPostActorJoined
    fun actorJoined(
        entrySpot: PlayEntrySpot,
        actor: PlayActor,
        info: ZLinkSpotActorChangeResult,
        cancellationToken: CancellationToken,
    ): CompletionStage<Void> = CompletableFuture.completedFuture(null)
}
