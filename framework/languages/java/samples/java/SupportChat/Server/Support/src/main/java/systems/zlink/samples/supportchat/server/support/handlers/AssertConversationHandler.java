package systems.zlink.samples.supportchat.server.support.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.support.domain.ConversationStore;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup(SampleNames.SupportChannel)
public final class AssertConversationHandler
    implements ZLinkRequestHandler<Messages.ServerAssertionRequest, Messages.ServerAssertionResponse> {
    private final ConversationStore store;

    public AssertConversationHandler(ConversationStore store) {
        this.store = store;
    }

    @Override
    public Messages.ServerAssertionResponse handle(
        Messages.ServerAssertionRequest request,
        ZLinkRequestContext context) {
        return store.assertConversation(request);
    }
}
