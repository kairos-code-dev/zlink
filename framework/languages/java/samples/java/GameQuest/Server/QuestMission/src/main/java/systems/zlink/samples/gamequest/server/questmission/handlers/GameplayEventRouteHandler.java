package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class GameplayEventRouteHandler
    implements ZLinkRouteRequestHandler<Messages.GameplayEventEnvelope, Messages.QuestProcessingRes> {
    private final QuestStore store;

    public GameplayEventRouteHandler(QuestStore store) {
        this.store = store;
    }

    @Override
    public Messages.QuestProcessingRes handle(
        Messages.GameplayEventEnvelope request,
        ZLinkRouteRequestContext context) {
        store.markRehydrated(request.playerId());
        return store.apply(request);
    }
}
