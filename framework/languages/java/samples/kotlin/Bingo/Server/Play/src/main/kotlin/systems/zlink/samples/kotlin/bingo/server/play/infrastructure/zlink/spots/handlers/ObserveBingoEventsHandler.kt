package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.BingoEntrySpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.ObserveBingoEventsReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.ObserveBingoEventsRes

class ObserveBingoEventsHandler : ZLinkSuspendingEntrySpotActorRequestHandler<
    BingoEntrySpot,
    PlayerActor,
    ObserveBingoEventsReq,
    ObserveBingoEventsRes,
    > {
    override suspend fun handle(
        entrySpot: BingoEntrySpot,
        actor: PlayerActor,
        context: ZLinkSpotActorRequestContext,
        request: ObserveBingoEventsReq,
        cancellationToken: CancellationToken,
    ): ObserveBingoEventsRes =
        entrySpot.observeEvents(actor, request)
}
