package systems.zlink.samples.supportchat.server.support.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.support.domain.ConversationStore;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup(SampleNames.SupportChannel)
public final class SetAgentAvailableHandler
    implements ZLinkRequestHandler<Messages.SetAgentAvailableReq, Messages.SetAgentAvailableRes> {
    private final ConversationStore store;

    public SetAgentAvailableHandler(ConversationStore store) {
        this.store = store;
    }

    @Override
    public Messages.SetAgentAvailableRes handle(Messages.SetAgentAvailableReq request, ZLinkRequestContext context) {
        return store.setAgentAvailable(request.isAvailable());
    }
}
