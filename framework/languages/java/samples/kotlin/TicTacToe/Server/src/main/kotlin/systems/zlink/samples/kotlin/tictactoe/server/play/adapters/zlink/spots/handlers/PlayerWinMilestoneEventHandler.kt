package systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots.handlers

import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots.PlayEntrySpot
import systems.zlink.samples.kotlin.tictactoe.shared.contracts.PlayerWinMilestoneEvent

class PlayerWinMilestoneEventHandler :
    ZLinkSpotSubscriptionHandler<PlayEntrySpot, PlayerWinMilestoneEvent> {
    override fun handle(spot: PlayEntrySpot, message: PlayerWinMilestoneEvent) {
        spot.notifyMilestone(message)
    }
}
