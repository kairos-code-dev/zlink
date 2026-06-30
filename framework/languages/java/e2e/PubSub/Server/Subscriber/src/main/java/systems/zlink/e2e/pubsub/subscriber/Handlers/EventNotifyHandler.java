package systems.zlink.e2e.pubsub.subscriber.Handlers;

import systems.zlink.e2e.pubsub.shared.Contracts;
import systems.zlink.e2e.pubsub.subscriber.Infrastructure.EvidenceStore;
import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
public final class EventNotifyHandler
    implements ZLinkPublishHandler<Contracts.EventNotify> {
    private final EvidenceStore evidence;

    public EventNotifyHandler(EvidenceStore evidence) {
        this.evidence = evidence;
    }

    @Override
    public void handle(
        Contracts.EventNotify message,
        ZLinkPublishContext context) {
        if (!evidence.accepts(context.topic())) {
            return;
        }
        evidence.delayIfConfigured(message.scenario());
        evidence.record(
            "EventNotify",
            context.topic(),
            message.scenario(),
            message.sequence(),
            message.value());
    }
}
