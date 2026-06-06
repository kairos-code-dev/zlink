package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.handlers

import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerTick
import systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots.BingoRoomSpot

class BingoRoomTimerHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpotTimerHandler<BingoRoomSpot>(coroutines) {
    override suspend fun handle(
        spot: BingoRoomSpot,
        tick: ZLinkTimerTick,
    ) {
        spot.tick()
    }
}
