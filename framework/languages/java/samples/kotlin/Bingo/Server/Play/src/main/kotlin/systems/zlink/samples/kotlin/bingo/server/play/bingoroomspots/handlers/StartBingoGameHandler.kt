package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.StartBingoGameReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.StartBingoGameRes

class StartBingoGameHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpotActorRequestHandler<
    BingoRoomSpot,
    PlayerActor,
    StartBingoGameReq,
    StartBingoGameRes,
    >(coroutines) {
    override suspend fun handle(
        spot: BingoRoomSpot,
        actor: PlayerActor,
        context: ZLinkSpotActorRequestContext,
        request: StartBingoGameReq,
        cancellationToken: CancellationToken,
    ): StartBingoGameRes =
        spot.start(actor, request)
}
