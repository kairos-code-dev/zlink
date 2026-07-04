package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class DeleteQuestProjectionHandler
    implements ZLinkRouteRequestHandler<Messages.DeleteQuestProjectionReq, Messages.DeleteQuestProjectionRes> {
    private final QuestStore store;

    public DeleteQuestProjectionHandler(QuestStore store) {
        this.store = store;
    }

    @Override
    public Messages.DeleteQuestProjectionRes handle(
        Messages.DeleteQuestProjectionReq request,
        ZLinkRouteRequestContext context) {
        return store.deleteProjection(request.playerId(), request.questId());
    }
}
