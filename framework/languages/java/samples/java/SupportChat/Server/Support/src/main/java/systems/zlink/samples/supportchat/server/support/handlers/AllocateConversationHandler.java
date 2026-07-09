package systems.zlink.samples.supportchat.server.support.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.support.domain.ConversationStore;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup(SampleNames.SupportChannel)
public final class AllocateConversationHandler
    implements ZLinkRequestHandler<Messages.AllocateConversationReq, Messages.AllocateConversationRes> {
    private final ConversationStore store;

    public AllocateConversationHandler(ConversationStore store) {
        this.store = store;
    }

    @Override
    public Messages.AllocateConversationRes handle(
        Messages.AllocateConversationReq request,
        ZLinkRequestContext context) {
        return store.allocate(request);
    }
}
