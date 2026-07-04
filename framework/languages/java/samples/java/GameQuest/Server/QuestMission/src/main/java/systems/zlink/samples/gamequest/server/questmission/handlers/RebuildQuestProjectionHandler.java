package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class RebuildQuestProjectionHandler
    implements ZLinkRouteRequestHandler<Messages.RebuildQuestProjectionReq, Messages.QuestProgress> {
    private final QuestStore store;

    public RebuildQuestProjectionHandler(QuestStore store) {
        this.store = store;
    }

    @Override
    public Messages.QuestProgress handle(
        Messages.RebuildQuestProjectionReq request,
        ZLinkRouteRequestContext context) {
        return store.rebuildProjection(request.playerId(), request.questId(), request.count());
    }
}
