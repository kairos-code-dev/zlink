package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.actors.ZLinkActor
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors.PlayerActor
import kotlinx.coroutines.future.await

class BingoEntrySpot(
    private val context: ZLinkEntrySpotContext,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkEntrySpot {
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() {}

    override fun onCreateActor(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) = Unit

    override fun onJoinActor(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor is PlayerActor && actor.destroyAfterEntrySpotJoin) {
            coroutines.blocking {
                context.destroyActor(actor).await()
            }
        }
    }

    override fun onLeaveActor(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) = Unit

    override fun onDisconnectActor(
        actor: ZLinkActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor is PlayerActor) {
            actor.markDisconnected()
        }
    }
}
