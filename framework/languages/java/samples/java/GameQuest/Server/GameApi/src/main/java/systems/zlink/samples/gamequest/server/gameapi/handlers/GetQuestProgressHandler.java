package systems.zlink.samples.gamequest.server.gameapi.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.infrastructure.store.GameQuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("gameapi")
public final class GetQuestProgressHandler
    implements ZLinkRequestHandler<Messages.GetQuestProgressReq, Messages.GetQuestProgressRes> {
    private final GameQuestStore store;

    public GetQuestProgressHandler(GameQuestStore store) {
        this.store = store;
    }

    @Override
    public Messages.GetQuestProgressRes handle(
        Messages.GetQuestProgressReq request, ZLinkRequestContext context) {
        return new Messages.GetQuestProgressRes(store.readProjection(request.playerId()));
    }
}
