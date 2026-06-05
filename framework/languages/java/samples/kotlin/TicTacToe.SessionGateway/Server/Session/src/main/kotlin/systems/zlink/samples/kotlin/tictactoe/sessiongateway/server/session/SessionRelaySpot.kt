package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.session

import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext

class SessionRelaySpot(private val context: ZLinkSpotContext) : ZLinkSpot {
    override fun context(): ZLinkSpotContext = context
}
