package systems.zlink.samples.gamequest.server.gameapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.application.GameplayActionService;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("gameapi")
public final class UnlockFeatureHandler
    implements ZLinkRequestHandler<Messages.UnlockFeatureReq, Messages.UnlockFeatureRes> {
    private final GameplayActionService actions;

    public UnlockFeatureHandler(GameplayActionService actions) {
        this.actions = actions;
    }

    @Override
    public Messages.UnlockFeatureRes handle(Messages.UnlockFeatureReq request, ZLinkRequestContext context) {
        return actions.unlockFeature(request);
    }
}
