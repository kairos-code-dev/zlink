package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.spots.PlayerQuestRouter;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class GameplayMsgHandler
    implements ZLinkRequestHandler<Messages.GameplayMsg, Messages.QuestProcessingRes> {
    private final PlayerQuestRouter owner;

    public GameplayMsgHandler(PlayerQuestRouter owner) {
        this.owner = owner;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.QuestProcessingRes> handle(
        Messages.GameplayMsg request,
        ZLinkRequestContext context) {
        return owner.request(request.playerId(), request, Messages.QuestProcessingRes.class);
    }
}
