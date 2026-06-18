package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers

import systems.zlink.framework.kotlin.ZLinkSuspendingSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerTick
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.BingoRoomSpot

class BingoRoomTimerHandler() : ZLinkSuspendingSpotTimerHandler<BingoRoomSpot> {
    override suspend fun handle(
        spot: BingoRoomSpot,
        tick: ZLinkTimerTick,
    ) = run {
        spot.tick()
    }
}
