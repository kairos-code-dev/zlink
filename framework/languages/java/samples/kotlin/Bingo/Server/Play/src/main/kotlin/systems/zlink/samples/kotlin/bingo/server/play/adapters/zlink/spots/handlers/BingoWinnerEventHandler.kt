package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers

import systems.zlink.framework.kotlin.ZLinkSuspendingSpotSubscriptionHandler
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoWinnerEvent

class BingoWinnerEventHandler :
    ZLinkSuspendingSpotSubscriptionHandler<BingoRoomSpot, BingoWinnerEvent> {
    override suspend fun handle(spot: BingoRoomSpot, event: BingoWinnerEvent) {
        spot.announceWinner(event)
    }
}
