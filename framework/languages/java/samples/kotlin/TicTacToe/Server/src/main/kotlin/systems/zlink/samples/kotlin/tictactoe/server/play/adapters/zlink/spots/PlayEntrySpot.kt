package systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.spots

import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.tictactoe.server.play.adapters.zlink.actors.PlayActor
import kotlinx.coroutines.future.await

class PlayEntrySpot(
    private val context: ZLinkEntrySpotContext,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkEntrySpot<PlayActor> {
    override fun context(): ZLinkEntrySpotContext = context

    override fun onCreateActor(
        actor: PlayActor,
        cancellationToken: CancellationToken,
    ) = Unit

    override fun onActorJoin(
        actor: PlayActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        ZLinkSpotActorJoinResponse.accept(Message.from(ByteArray(0)))

    override fun onJoinedActor(
        actor: PlayActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor.destroyAfterEntrySpotJoin) {
            coroutines.blocking {
                context.destroyActor(actor).await()
            }
        }
    }

    override fun onDisconnectActor(
        actor: PlayActor,
        cancellationToken: CancellationToken,
    ) {
        actor.markDisconnected()
    }
}
