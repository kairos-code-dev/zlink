package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.SubmitBingoCardRes

class SubmitBingoCardHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpotActorRequestHandler<
    BingoRoomSpot,
    PlayerActor,
    SubmitBingoCardReq,
    SubmitBingoCardRes,
    >(coroutines) {
    override suspend fun handle(
        spot: BingoRoomSpot,
        actor: PlayerActor,
        context: ZLinkSpotActorRequestContext,
        request: SubmitBingoCardReq,
        cancellationToken: CancellationToken,
    ): SubmitBingoCardRes =
        spot.submitCard(actor, request)
}
