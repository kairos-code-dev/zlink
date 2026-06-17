package systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.spots

import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.bingo.server.play.adapters.zlink.actors.PlayerActor
import kotlinx.coroutines.future.await

class BingoEntrySpot(
    private val context: ZLinkEntrySpotContext,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkEntrySpot<PlayerActor> {
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() {}

    override fun onCreateActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) = Unit

    override fun onActorJoin(
        actor: PlayerActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        ZLinkSpotActorJoinResponse.accept(Message.from(ByteArray(0)))

    override fun onJoinedActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor.destroyAfterEntrySpotJoin) {
            coroutines.blocking {
                context.destroyActor(actor).await()
            }
        }
    }

    override fun onLeaveActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) = Unit

    override fun onDisconnectActor(
        actor: PlayerActor,
        cancellationToken: CancellationToken,
    ) {
        actor.markDisconnected()
    }
}
