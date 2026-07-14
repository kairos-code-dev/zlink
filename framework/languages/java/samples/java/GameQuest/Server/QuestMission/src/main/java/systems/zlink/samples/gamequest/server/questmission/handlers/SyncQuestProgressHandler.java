package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.spots.PlayerQuestRouter;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class SyncQuestProgressHandler
    implements ZLinkRequestHandler<Messages.SyncQuestProgressReq, Messages.SyncQuestProgressRes> {
    private final PlayerQuestRouter owner;

    public SyncQuestProgressHandler(PlayerQuestRouter owner) {
        this.owner = owner;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.SyncQuestProgressRes> handle(
        Messages.SyncQuestProgressReq request,
        ZLinkRequestContext context) {
        return owner.request(request.playerId(), request, Messages.SyncQuestProgressRes.class);
    }
}
