package systems.zlink.samples.kotlin.tictactoe.sessiongateway.server.play.entryspot

import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext

class TicTacToeEntrySpot(
    private val context: ZLinkEntrySpotContext? = null,
) : ZLinkEntrySpot {
    override fun context(): ZLinkEntrySpotContext? = context
}
