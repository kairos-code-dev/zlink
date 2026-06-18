package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers

import systems.zlink.framework.kotlin.ZLinkSuspendingSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerTick
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.ConversationSpot

class ConversationIdleTimerHandler() : ZLinkSuspendingSpotTimerHandler<ConversationSpot> {
    override suspend fun handle(
        spot: ConversationSpot,
        tick: ZLinkTimerTick,
    ) = run {
        spot.checkIdle()
    }
}
