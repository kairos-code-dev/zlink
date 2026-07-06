package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class SyncQuestProgressHandler
    implements ZLinkRequestHandler<Messages.SyncQuestProgressReq, Messages.SyncQuestProgressRes> {
    private final QuestStore store;

    public SyncQuestProgressHandler(QuestStore store) {
        this.store = store;
    }

    @Override
    public Messages.SyncQuestProgressRes handle(
        Messages.SyncQuestProgressReq request,
        ZLinkRequestContext context) {
        return store.sync(request.playerId(), 4);
    }
}
