package systems.zlink.samples.kotlin.bingo.server.play.entryspot

import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.samples.kotlin.bingo.server.play.entryspot.handlers.BingoEntrySpotActorJoinedHandler
import systems.zlink.samples.kotlin.bingo.server.play.entryspot.handlers.BingoEntrySpotActorLeftHandler
import systems.zlink.samples.kotlin.bingo.server.play.entryspot.handlers.MatchBingoActorHandler

class BingoEntrySpot(
    private val context: ZLinkEntrySpotContext,
) : ZLinkEntrySpot {
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() {
        context.handlers().addHandler(MatchBingoActorHandler::class.java)
        context.handlers().addHandler(BingoEntrySpotActorJoinedHandler::class.java)
        context.handlers().addHandler(BingoEntrySpotActorLeftHandler::class.java)
    }
}
