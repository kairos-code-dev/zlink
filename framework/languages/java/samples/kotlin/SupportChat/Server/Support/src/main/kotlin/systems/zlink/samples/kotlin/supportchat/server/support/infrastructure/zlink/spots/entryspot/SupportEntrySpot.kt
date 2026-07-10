package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.entryspot

import systems.zlink.framework.CancellationToken
import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.messaging.ZLinkMessage
import systems.zlink.framework.spots.ZLinkEntrySpot
import systems.zlink.framework.spots.ZLinkEntrySpotContext
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.samples.kotlin.supportchat.server.configuration.SupportChatRoles
import systems.zlink.samples.kotlin.supportchat.server.support.application.AgentAssignmentService
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportActorDirectory
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.shared.contracts.EnsureSupportUserActorReq

class SupportEntrySpot(
    private val context: ZLinkEntrySpotContext,
    private val directory: SupportActorDirectory,
    private val assignment: AgentAssignmentService,
    private val actorManager: ZLinkActorManager,
) : ZLinkEntrySpot<SupportUserActor> {
    override fun context(): ZLinkEntrySpotContext = context

    override fun onCreateActor(
        actor: SupportUserActor,
        createRequest: ZLinkMessage,
        cancellationToken: CancellationToken,
    ) {
        val request = createRequest.decode(EnsureSupportUserActorReq::class.java)
        actor.setIdentity(request.displayName, request.role, request.participantId)
        val actorRef = ZLinkAwait.await(actorManager.find(actor.actorId)).orElse(null)
            ?: throw IllegalStateException("Support actor ref is not available. actor=${actor.actorId}")
        directory.addOrUpdate(actor, actorRef)
    }

    override fun onActorJoin(
        actorId: String,
        request: ZLinkMessage,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse = ZLinkSpotActorJoinResponse.accept(request)

    override fun onJoinedActor(
        actor: SupportUserActor,
        cancellationToken: CancellationToken,
    ) {
    }

    override fun onLeaveActor(
        actor: SupportUserActor,
        cancellationToken: CancellationToken,
    ) {
    }

    override fun onDisconnectActor(
        actor: SupportUserActor,
        cancellationToken: CancellationToken,
    ) {
        if (actor.role == SupportChatRoles.Agent) {
            assignment.setAvailable(actor.actorId, actor.displayName, false)
        }
    }
}
