package systems.zlink.samples.gamequest.server.gameapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.application.GameplayActionService;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("gameapi")
public final class KillMonsterHandler
    implements ZLinkRequestHandler<Messages.KillMonsterReq, Messages.KillMonsterRes> {
    private final GameplayActionService actions;

    public KillMonsterHandler(GameplayActionService actions) {
        this.actions = actions;
    }

    @Override
    public Messages.KillMonsterRes handle(Messages.KillMonsterReq request, ZLinkRequestContext context) {
        return actions.killMonster(request);
    }
}
