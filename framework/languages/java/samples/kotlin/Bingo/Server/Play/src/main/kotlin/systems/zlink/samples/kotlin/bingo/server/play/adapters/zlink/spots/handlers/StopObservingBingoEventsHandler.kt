package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.StopObservingBingoEventsReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.StopObservingBingoEventsRes

class StopObservingBingoEventsHandler : ZLinkSuspendingSpotActorRequestHandler<
    BingoRoomSpot,
    PlayerActor,
    StopObservingBingoEventsReq,
    StopObservingBingoEventsRes,
    > {
    override suspend fun handle(
        spot: BingoRoomSpot,
        actor: PlayerActor,
        context: ZLinkSpotActorRequestContext,
        request: StopObservingBingoEventsReq,
        cancellationToken: CancellationToken,
    ): StopObservingBingoEventsRes =
        spot.stopObserving(actor, request)
}
