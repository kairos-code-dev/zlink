package systems.zlink.samples.supportchat.server.support.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.support.domain.ConversationStore;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup(SampleNames.SupportChannel)
public final class SetTypingHandler
    implements ZLinkRequestHandler<Messages.SetTypingSupportReq, Messages.ConversationState> {
    private final ConversationStore store;

    public SetTypingHandler(ConversationStore store) {
        this.store = store;
    }

    @Override
    public Messages.ConversationState handle(
        Messages.SetTypingSupportReq request,
        ZLinkRequestContext context) {
        return store.typing(request);
    }
}
