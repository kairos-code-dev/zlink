package systems.zlink.samples.gamequest.server.gameapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.application.GameplayActionService;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("gameapi")
public final class CompleteMissionHandler
    implements ZLinkRequestHandler<Messages.CompleteMissionReq, Messages.CompleteMissionRes> {
    private final GameplayActionService actions;

    public CompleteMissionHandler(GameplayActionService actions) {
        this.actions = actions;
    }

    @Override
    public Messages.CompleteMissionRes handle(Messages.CompleteMissionReq request, ZLinkRequestContext context) {
        return actions.completeMission(request);
    }
}
