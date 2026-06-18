package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.supportchat.server.configuration.ConversationStatuses
import systems.zlink.samples.kotlin.supportchat.server.support.application.assignment.SupportConversationAllocator
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AllocateConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AllocateConversationRes

@ZLinkHandlerGroup("support")
class AllocateConversationHandler(
    private val allocator: SupportConversationAllocator,
) : ZLinkSuspendingRequestHandler<AllocateConversationReq, AllocateConversationRes> {
    override suspend fun handle(
        request: AllocateConversationReq,
        context: ZLinkRequestContext,
    ) = run {
        val conversationId = allocator.allocate(
            request.customerActorId,
            request.customerDisplayName,
            request.subject,
        )
        AllocateConversationRes(conversationId, ConversationStatuses.WaitingForAgent)
    }
}
