package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class GameplayEventHandler
    implements ZLinkRequestHandler<Messages.GameplayEventEnvelope, Messages.QuestProcessingRes> {
    private final QuestStore store;

    public GameplayEventHandler(QuestStore store) {
        this.store = store;
    }

    @Override
    public Messages.QuestProcessingRes handle(
        Messages.GameplayEventEnvelope request,
        ZLinkRequestContext context) {
        store.markRehydrated(request.playerId());
        return store.apply(request);
    }
}
