package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class SyncQuestProgressHandler
    implements ZLinkRouteRequestHandler<Messages.SyncQuestProgressReq, Messages.SyncQuestProgressRes> {
    private final QuestStore store;

    public SyncQuestProgressHandler(QuestStore store) {
        this.store = store;
    }

    @Override
    public Messages.SyncQuestProgressRes handle(
        Messages.SyncQuestProgressReq request,
        ZLinkRouteRequestContext context) {
        return store.sync(request.playerId(), 4);
    }
}
