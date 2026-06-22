package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.support.application.assignment.SupportConversationAllocator;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.Statuses;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup("support")
public final class AllocateConversationHandler
    implements ZLinkRequestHandler<
        Messages.AllocateConversationReq,
        Messages.AllocateConversationRes> {
    private final SupportConversationAllocator allocator;

    public AllocateConversationHandler(SupportConversationAllocator allocator) {
        this.allocator = allocator;
    }

    @Override
    public Messages.AllocateConversationRes handle(
        Messages.AllocateConversationReq request,
        ZLinkRequestContext context) {
        String conversationId = allocator.allocate(
            request.customerActorId(),
            request.customerDisplayName(),
            request.subject());
        return new Messages.AllocateConversationRes(conversationId, Statuses.WaitingForAgent);
    }
}
