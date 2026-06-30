package systems.zlink.samples.gamequest.server.questmission.handlers;

import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.questmission.application.QuestEventProcessor;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/** Fanout subscriber: processes published gameplay events. Mirrors the .NET {@code GameplayEventHandler}. */
@ZLinkHandlerGroup("gamequest-gameplay")
public final class GameplayEventHandler implements ZLinkPublishHandler<Messages.GameplayEventMsg> {
    private final QuestEventProcessor processor;

    public GameplayEventHandler(QuestEventProcessor processor) {
        this.processor = processor;
    }

    @Override
    public void handle(Messages.GameplayEventMsg message, ZLinkPublishContext context) {
        if (!SampleNames.GameplayTopic.equals(context.topic())) {
            return;
        }
        processor.process(message);
    }
}
