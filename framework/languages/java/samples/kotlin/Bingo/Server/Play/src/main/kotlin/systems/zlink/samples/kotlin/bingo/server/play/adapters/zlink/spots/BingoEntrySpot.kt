package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors.PlayerActor
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots.handlers.MatchBingoActorHandler

class BingoEntrySpot(
    private val context: ZLinkEntrySpotContext,
) : ZLinkEntrySpot {
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() {
        context.handlers().addHandler(MatchBingoActorHandler::class.java)
    }

    override fun onPostActorJoined(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor is PlayerActor && actor.destroyAfterEntrySpotJoin) {
            ZLinkAwait.await(context.destroyActorAsync(actor))
        }
    }

    override fun onActorLeft(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) = Unit
}
