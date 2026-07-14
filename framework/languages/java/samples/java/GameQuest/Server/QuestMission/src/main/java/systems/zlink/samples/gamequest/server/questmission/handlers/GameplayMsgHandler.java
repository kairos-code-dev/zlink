package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.spots.PlayerQuestRouter;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-owner")
public final class GameplayMsgHandler
    implements ZLinkSendHandler<Messages.GameplayMsg> {
    private final PlayerQuestRouter owner;

    public GameplayMsgHandler(PlayerQuestRouter owner) {
        this.owner = owner;
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> handle(
        Messages.GameplayMsg request,
        ZLinkSendContext context) {
        return owner.send(request.playerId(), request);
    }
}
