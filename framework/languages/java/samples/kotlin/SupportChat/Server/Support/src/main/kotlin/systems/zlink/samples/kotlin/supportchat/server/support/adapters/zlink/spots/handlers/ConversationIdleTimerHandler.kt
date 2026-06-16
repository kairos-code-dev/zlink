package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers

import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.kotlin.ZLinkCoroutineSpotTimerHandler
import systems.zlink.framework.spots.ZLinkTimerTick
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.ConversationSpot

class ConversationIdleTimerHandler(
    coroutines: ZLinkCoroutineRuntime,
) : ZLinkCoroutineSpotTimerHandler<ConversationSpot>(coroutines) {
    override suspend fun handleSuspending(
        spot: ConversationSpot,
        tick: ZLinkTimerTick,
    ) {
        spot.checkIdle()
    }
}
