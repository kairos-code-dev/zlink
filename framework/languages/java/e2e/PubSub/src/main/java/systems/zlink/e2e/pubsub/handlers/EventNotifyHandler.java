package systems.zlink.e2e.pubsub.handlers;

import systems.zlink.e2e.pubsub.Contracts;
import systems.zlink.e2e.pubsub.ScenarioState;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class EventNotifyHandler
    implements ZLinkPublishHandler<Contracts.EventNotify> {
    private final ScenarioState state;

    public EventNotifyHandler(ScenarioState state) {
        this.state = state;
    }

    @Override
    public void handle(
        Contracts.EventNotify message,
        ZLinkPublishContext context) {
        if (!state.accepts(context.topic())) {
            return;
        }
        state.record(
            "EventNotify",
            context.topic(),
            message.scenario(),
            message.sequence(),
            message.value());
    }
}
