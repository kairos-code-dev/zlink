package systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.entryspot.handlers

import kotlinx.coroutines.future.await
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.kotlin.ZLinkSuspendingEntrySpotActorRequestHandler
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportActorDirectory
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.infrastructure.zlink.spots.entryspot.SupportEntrySpot
import systems.zlink.samples.kotlin.supportchat.server.support.application.assignment.AgentAvailabilityDirectory
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.Roles
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetAgentAvailableReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetAgentAvailableRes

class SetAgentAvailableHandler(
    private val availability: AgentAvailabilityDirectory,
    private val actors: SupportActorDirectory,
    private val actorManager: ZLinkActorManager,
) : ZLinkSuspendingEntrySpotActorRequestHandler<
    SupportEntrySpot,
    SupportUserActor,
    SetAgentAvailableReq,
    SetAgentAvailableRes,
    > {
    override suspend fun handle(
        entrySpot: SupportEntrySpot,
        actor: SupportUserActor,
        context: ZLinkSpotActorRequestContext,
        request: SetAgentAvailableReq,
        cancellationToken: CancellationToken,
    ): SetAgentAvailableRes {
        check(Roles.Agent == actor.role) { "Only agent actors can set availability." }
        val ref = actorManager.find(actor.actorId()).await()
            .orElseThrow { IllegalStateException("Support actor ref is not available.") }
        actors.addOrUpdate(actor, ref)
        availability.setAvailable(actor.actorId(), actor.displayName, request.isAvailable)
        return SetAgentAvailableRes(request.isAvailable)
    }
}
