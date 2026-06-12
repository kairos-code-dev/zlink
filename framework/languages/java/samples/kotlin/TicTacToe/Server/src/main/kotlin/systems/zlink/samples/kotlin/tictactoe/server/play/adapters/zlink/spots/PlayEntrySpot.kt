package systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.actors.PlayActor

class PlayEntrySpot(
    private val context: ZLinkEntrySpotContext,
) : ZLinkEntrySpot {
    override fun context(): ZLinkEntrySpotContext = context

    override fun onPostActorJoined(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor is PlayActor && actor.destroyAfterEntrySpotJoin) {
            ZLinkAwait.await(context.destroyActorAsync(actor))
        }
    }
}
