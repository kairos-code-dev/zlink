package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.spots.PlayerQuestRouter;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class RebuildQuestProjectionHandler
    implements ZLinkRequestHandler<Messages.RebuildQuestProjectionReq, Messages.QuestProgress> {
    private final PlayerQuestRouter owner;

    public RebuildQuestProjectionHandler(PlayerQuestRouter owner) {
        this.owner = owner;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.QuestProgress> handle(
        Messages.RebuildQuestProjectionReq request,
        ZLinkRequestContext context) {
        return owner.request(request.playerId(), request, Messages.QuestProgress.class);
    }
}
