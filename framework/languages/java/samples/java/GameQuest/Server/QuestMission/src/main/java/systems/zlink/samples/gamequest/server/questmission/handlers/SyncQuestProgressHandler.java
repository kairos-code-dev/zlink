package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.questmission.application.QuestEventProcessor;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/** Mission action channel handler: reconciles a player from the GameApi snapshot. */
@ZLinkHandlerGroup("gamequest-mission")
public final class SyncQuestProgressHandler
    implements ZLinkRequestHandler<Messages.SyncQuestProgressReq, Messages.SyncQuestProgressRes> {
    private final QuestEventProcessor processor;

    public SyncQuestProgressHandler(QuestEventProcessor processor) {
        this.processor = processor;
    }

    @Override
    public Messages.SyncQuestProgressRes handle(
        Messages.SyncQuestProgressReq request, ZLinkRequestContext context) {
        return processor.sync(request);
    }
}
