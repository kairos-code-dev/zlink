package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.spots.PlayerQuestRouter;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class GetQuestProgressHandler
    implements ZLinkRequestHandler<Messages.GetQuestProgressReq, Messages.GetQuestProgressRes> {
    private final PlayerQuestRouter owner;

    public GetQuestProgressHandler(PlayerQuestRouter owner) {
        this.owner = owner;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.GetQuestProgressRes> handle(
        Messages.GetQuestProgressReq request,
        ZLinkRequestContext context) {
        return owner.request(request.playerId(), request, Messages.GetQuestProgressRes.class);
    }
}
