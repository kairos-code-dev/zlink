package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.supportchat.server.configuration.ConversationStatuses
import systems.zlink.samples.kotlin.supportchat.server.support.application.assignment.SupportConversationAllocator
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AllocateConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AllocateConversationRes

@ZLinkHandlerGroup("support")
class AllocateConversationHandler(
    private val allocator: SupportConversationAllocator,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<AllocateConversationReq, AllocateConversationRes> {
    override fun handle(
        request: AllocateConversationReq,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        val conversationId = allocator.allocate(
            request.customerActorId,
            request.customerDisplayName,
            request.subject,
        )
        AllocateConversationRes(conversationId, ConversationStatuses.WaitingForAgent)
    }
}
