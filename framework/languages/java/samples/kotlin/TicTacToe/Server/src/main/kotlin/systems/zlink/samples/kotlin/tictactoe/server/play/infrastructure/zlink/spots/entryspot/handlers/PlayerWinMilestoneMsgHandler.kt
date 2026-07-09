package systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.entryspot.handlers

import systems.zlink.framework.handlers.ZLinkSpotSubscription
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler
import systems.zlink.samples.kotlin.tictactoe.server.configuration.SampleNames
import systems.zlink.samples.kotlin.tictactoe.server.play.infrastructure.zlink.spots.entryspot.PlayEntrySpot
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerWinMilestoneMsg

@ZLinkSpotSubscription(topic = SampleNames.PlayerMilestoneTopic)
class PlayerWinMilestoneMsgHandler :
    ZLinkSpotSubscriptionHandler<PlayEntrySpot, PlayerWinMilestoneMsg> {
    override fun handle(spot: PlayEntrySpot, message: PlayerWinMilestoneMsg) {
        spot.notifyMilestone(message)
    }
}
