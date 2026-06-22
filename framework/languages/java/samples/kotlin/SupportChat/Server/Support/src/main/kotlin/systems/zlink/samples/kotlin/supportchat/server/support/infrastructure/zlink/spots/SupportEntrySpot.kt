package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots

import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor

class SupportEntrySpot(
    private val context: ZLinkEntrySpotContext,
) : ZLinkEntrySpot<SupportUserActor> {
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() = Unit

    override fun onCreateActor(actor: SupportUserActor, cancellationToken: CancellationToken) = Unit

    override fun onActorJoin(
        actor: SupportUserActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        ZLinkSpotActorJoinResponse.accept(Message.from(ByteArray(0)))

    override fun onJoinedActor(actor: SupportUserActor, cancellationToken: CancellationToken) = Unit

    override fun onLeaveActor(actor: SupportUserActor, cancellationToken: CancellationToken) = Unit

    override fun onDisconnectActor(actor: SupportUserActor, cancellationToken: CancellationToken) {
        actor.markDisconnected()
    }
}
