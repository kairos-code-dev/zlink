package systems.zlink.samples.kotlin.bingo.server.play.bingoroomspots

import systems.zlink.framework.spots.ZLinkSpot
import systems.zlink.framework.spots.ZLinkSpotContext

class BingoRoomSpot(
    private val context: ZLinkSpotContext,
) : ZLinkSpot {
    override fun context(): ZLinkSpotContext = context

    companion object {
        fun cardFor(playerId: String): BingoCard =
            when (playerId) {
                "player-2", "player-3" -> BingoCard(listOf(7, 11, 42))
                else -> BingoCard(listOf(1, 2, 3))
            }
    }
}
