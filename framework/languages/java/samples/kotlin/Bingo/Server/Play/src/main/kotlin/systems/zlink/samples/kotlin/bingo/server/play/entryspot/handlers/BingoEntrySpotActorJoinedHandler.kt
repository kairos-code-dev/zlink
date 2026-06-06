package systems.zlink.samples.kotlin.bingo.server.play.entryspot.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpotPostActorJoinedHandler
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.entryspot.BingoEntrySpot

class BingoEntrySpotActorJoinedHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpotPostActorJoinedHandler<BingoEntrySpot, PlayerActor>(coroutines) {
    override suspend fun handle(
        spot: BingoEntrySpot,
        actor: PlayerActor,
        result: ZLinkSpotActorChangeResult,
        cancellationToken: CancellationToken,
    ) {
    }
}
