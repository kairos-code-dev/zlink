package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpotActorJoinHandler
import systems.zlink.samples.kotlin.bingo.server.play.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinReq
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoRoomJoinRes

class BingoRoomJoinHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpotActorJoinHandler<
    BingoRoomSpot,
    PlayerActor,
    BingoRoomJoinReq,
    BingoRoomJoinRes,
    >(coroutines) {
    override suspend fun handle(
        spot: BingoRoomSpot,
        actor: PlayerActor,
        request: BingoRoomJoinReq,
        cancellationToken: CancellationToken,
    ): BingoRoomJoinRes {
        return spot.join(actor, request)
    }
}
