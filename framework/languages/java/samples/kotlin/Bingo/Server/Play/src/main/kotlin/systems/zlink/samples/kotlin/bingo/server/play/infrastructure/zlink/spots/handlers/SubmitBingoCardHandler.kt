package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardRes

class SubmitBingoCardHandler() : ZLinkSuspendingSpotActorRequestHandler<
    BingoRoomSpot,
    PlayerActor,
    SubmitBingoCardReq,
    SubmitBingoCardRes,
    > {
    override suspend fun handle(
        spot: BingoRoomSpot,
        actor: PlayerActor,
        context: ZLinkSpotActorRequestContext,
        request: SubmitBingoCardReq,
        cancellationToken: CancellationToken,
    ): SubmitBingoCardRes =
        spot.submitCard(actor, request)
}
