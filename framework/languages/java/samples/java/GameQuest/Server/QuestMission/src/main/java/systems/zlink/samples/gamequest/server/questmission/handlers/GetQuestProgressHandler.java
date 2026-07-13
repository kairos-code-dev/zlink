package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class GetQuestProgressHandler
    implements ZLinkRequestHandler<Messages.GetQuestProgressReq, Messages.GetQuestProgressRes> {
    private final QuestStore store;

    public GetQuestProgressHandler(QuestStore store) {
        this.store = store;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.GetQuestProgressRes> handle(
        Messages.GetQuestProgressReq request,
        ZLinkRequestContext context) {
        return java.util.concurrent.CompletableFuture.completedFuture(
            new Messages.GetQuestProgressRes(store.projection(request.playerId())));
    }
}
