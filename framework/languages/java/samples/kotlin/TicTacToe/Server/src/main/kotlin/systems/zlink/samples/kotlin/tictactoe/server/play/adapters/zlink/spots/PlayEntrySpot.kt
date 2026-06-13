package systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.actors.PlayActor
import kotlinx.coroutines.future.await

class PlayEntrySpot(
    private val context: ZLinkEntrySpotContext,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkEntrySpot {
    override fun context(): ZLinkEntrySpotContext = context

    override fun onCreateActor(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) = Unit

    override fun onJoinActor(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor is PlayActor && actor.destroyAfterEntrySpotJoin) {
            coroutines.blocking {
                context.destroyActor(actor).await()
            }
        }
    }

    override fun onDisconnectActor(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor is PlayActor) {
            actor.markDisconnected()
        }
    }
}
