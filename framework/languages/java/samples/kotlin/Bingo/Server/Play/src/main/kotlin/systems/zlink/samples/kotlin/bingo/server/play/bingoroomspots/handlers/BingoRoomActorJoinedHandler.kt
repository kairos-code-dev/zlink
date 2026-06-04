package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.framework.handlers.ZLinkSpotPostActorJoined
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor

class BingoRoomActorJoinedHandler {
    @ZLinkSpotPostActorJoined
    fun handleAsync(
        actor: PlayerActor,
        result: ZLinkSpotActorChangeResult,
    ): CompletionStage<Void> =
        CompletableFuture.completedFuture(null)
}
