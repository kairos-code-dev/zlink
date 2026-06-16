package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.handlers

import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.supportchat.server.configuration.ConversationStatuses
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportActorDirectory
import systems.zlink.samples.kotlin.supportchat.server.support.application.assignment.AgentAssignmentService
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AssignAgentReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AssignAgentRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.JoinConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.JoinConversationRes

@ZLinkHandlerGroup("support")
class AssignAgentHandler(
    private val assignment: AgentAssignmentService,
    private val actors: SupportActorDirectory,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<AssignAgentReq, AssignAgentRes> {
    override fun handle(
        request: AssignAgentReq,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        val assigned = assignment.assignNextAgent()
            ?: return@blocking AssignAgentRes(
                request.conversationId,
                ConversationStatuses.WaitingForAgent,
                null,
            )
        val actor = actors.get(assigned.actorId)
        val joined = actor.context()
            .joinSpot(
                RoutingId.from(request.conversationId),
                JoinConversationReq(request.conversationId),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(JoinConversationRes::class.java)
            .await()
        AssignAgentRes(request.conversationId, joined.reply().state.status, actor.actorId())
    }
}
