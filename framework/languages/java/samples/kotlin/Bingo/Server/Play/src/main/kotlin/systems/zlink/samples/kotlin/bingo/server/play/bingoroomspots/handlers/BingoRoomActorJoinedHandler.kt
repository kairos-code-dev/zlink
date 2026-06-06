package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpotPostActorJoinedHandler
import systems.zlink.framework.spots.ZLinkSpotActorChangeResult
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot

class BingoRoomActorJoinedHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpotPostActorJoinedHandler<BingoRoomSpot, PlayerActor>(coroutines) {
    override suspend fun handle(
        spot: BingoRoomSpot,
        actor: PlayerActor,
        result: ZLinkSpotActorChangeResult,
        cancellationToken: CancellationToken,
    ) {
    }
}
