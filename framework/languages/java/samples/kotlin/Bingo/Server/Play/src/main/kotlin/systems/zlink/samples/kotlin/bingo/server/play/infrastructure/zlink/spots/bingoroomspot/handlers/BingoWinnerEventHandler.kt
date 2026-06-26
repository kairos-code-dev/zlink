package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers

import systems.zlink.framework.kotlin.ZLinkSuspendingSpotSubscriptionHandler
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoWinnerEvent

class BingoWinnerEventHandler :
    ZLinkSuspendingSpotSubscriptionHandler<BingoRoomSpot, BingoWinnerEvent> {
    override suspend fun handle(spot: BingoRoomSpot, event: BingoWinnerEvent) {
        spot.announceWinner(event)
    }
}
