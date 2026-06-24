package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.ZLinkAwait.await
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportActorDirectory
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureSupportUserActorReq

class SupportEntrySpot(
    private val context: ZLinkEntrySpotContext,
    private val actors: ZLinkActorManager,
    private val directory: SupportActorDirectory,
) : ZLinkEntrySpot<SupportUserActor> {
    override fun context(): ZLinkEntrySpotContext = context

    override fun configure() = Unit

    override fun onCreateActor(
        actor: SupportUserActor,
        createRequest: ZLinkMessage,
        cancellationToken: CancellationToken,
    ) {
        val request = createRequest.decode(EnsureSupportUserActorReq::class.java)
        actor.setIdentity(request.displayName, request.role)
        val ref = await(actors.find(actor.actorId()))
            .orElseThrow { IllegalStateException("Support actor ref is not available.") }
        directory.addOrUpdate(actor, ref)
    }

    override fun onActorJoin(
        actor: SupportUserActor,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse =
        ZLinkSpotActorJoinResponse.accept()

    override fun onJoinedActor(actor: SupportUserActor, cancellationToken: CancellationToken) = Unit

    override fun onLeaveActor(actor: SupportUserActor, cancellationToken: CancellationToken) = Unit

    override fun onDisconnectActor(actor: SupportUserActor, cancellationToken: CancellationToken) {
        actor.markDisconnected()
    }
}
