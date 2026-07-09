package systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.handlers

import systems.zlink.framework.handlers.ZLinkSpotSubscription
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotSubscriptionHandler
import systems.zlink.samples.kotlin.bingo.server.configuration.SampleNames
import systems.zlink.samples.kotlin.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot
import systems.zlink.samples.kotlin.bingo.shared.contracts.BingoWinnerMsg

@ZLinkSpotSubscription(topic = SampleNames.WinnerTopic)
class BingoWinnerMsgHandler :
    ZLinkSuspendingSpotSubscriptionHandler<BingoRoomSpot, BingoWinnerMsg> {
    override suspend fun handle(spot: BingoRoomSpot, event: BingoWinnerMsg) {
        spot.announceWinner(event)
    }
}
