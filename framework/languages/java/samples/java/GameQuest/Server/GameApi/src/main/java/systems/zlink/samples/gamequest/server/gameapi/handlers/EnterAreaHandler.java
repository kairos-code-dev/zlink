package systems.zlink.samples.gamequest.server.gameapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.application.GameplayActionService;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("gameapi")
public final class EnterAreaHandler
    implements ZLinkRequestHandler<Messages.EnterAreaReq, Messages.EnterAreaRes> {
    private final GameplayActionService actions;

    public EnterAreaHandler(GameplayActionService actions) {
        this.actions = actions;
    }

    @Override
    public Messages.EnterAreaRes handle(Messages.EnterAreaReq request, ZLinkRequestContext context) {
        return actions.enterArea(request);
    }
}
