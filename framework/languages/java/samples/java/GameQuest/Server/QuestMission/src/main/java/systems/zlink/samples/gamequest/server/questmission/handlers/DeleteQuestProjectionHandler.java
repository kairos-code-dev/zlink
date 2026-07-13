package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.store.QuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class DeleteQuestProjectionHandler
    implements ZLinkRequestHandler<Messages.DeleteQuestProjectionReq, Messages.DeleteQuestProjectionRes> {
    private final QuestStore store;

    public DeleteQuestProjectionHandler(QuestStore store) {
        this.store = store;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.DeleteQuestProjectionRes> handle(
        Messages.DeleteQuestProjectionReq request,
        ZLinkRequestContext context) {
        return java.util.concurrent.CompletableFuture.completedFuture(
            store.deleteProjection(request.playerId(), request.questId()));
    }
}
