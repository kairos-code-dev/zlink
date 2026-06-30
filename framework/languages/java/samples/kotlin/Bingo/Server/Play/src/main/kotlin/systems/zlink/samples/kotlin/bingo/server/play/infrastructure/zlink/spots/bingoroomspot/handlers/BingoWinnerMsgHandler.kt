package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers

import systems.zlink.framework.kotlin.ZLinkSuspendingSpotSubscriptionHandler
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoWinnerMsg

class BingoWinnerMsgHandler :
    ZLinkSuspendingSpotSubscriptionHandler<BingoRoomSpot, BingoWinnerMsg> {
    override suspend fun handle(spot: BingoRoomSpot, event: BingoWinnerMsg) {
        spot.announceWinner(event)
    }
}
