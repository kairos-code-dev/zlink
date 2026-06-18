package systems.zlink.samples.gamequest.server.gameapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.application.GameplayActionService;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("gameapi")
public final class CollectItemHandler
    implements ZLinkRequestHandler<Messages.CollectItemReq, Messages.CollectItemRes> {
    private final GameplayActionService actions;

    public CollectItemHandler(GameplayActionService actions) {
        this.actions = actions;
    }

    @Override
    public Messages.CollectItemRes handle(Messages.CollectItemReq request, ZLinkRequestContext context) {
        return actions.collectItem(request);
    }
}
