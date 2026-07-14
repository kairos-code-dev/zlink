package systems.zlink.samples.gamequest.server.gameapi.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkSendContext;
import systems.zlink.framework.channels.ZLinkSendHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.gameapi.sessions.GameQuestSessionRegistry;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

@ZLinkHandlerGroup("quest-notify")
public final class QuestProcessingMsgHandler
    implements ZLinkSendHandler<Messages.QuestProcessingMsg> {
    private final GameQuestSessionRegistry sessions;

    public QuestProcessingMsgHandler(GameQuestSessionRegistry sessions) {
        this.sessions = sessions;
    }

    @Override
    public CompletionStage<Void> handle(
        Messages.QuestProcessingMsg message,
        ZLinkSendContext context) {
        return sessions.deliver(message);
    }
}
