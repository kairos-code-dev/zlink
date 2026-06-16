package systems.zlink.samples.deliverydispatch.server.session.sessions.handlers;

import systems.zlink.framework.channels.ZLinkPublishContext;
import systems.zlink.framework.channels.ZLinkPublishHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.session.sessions.CustomerSessionDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("status")
public final class DeliveryStatusFanoutHandler
    implements ZLinkPublishHandler<Messages.DeliveryStatusNotify> {
    private final CustomerSessionDirectory sessions;

    public DeliveryStatusFanoutHandler(CustomerSessionDirectory sessions) {
        this.sessions = sessions;
    }

    @Override
    public void handle(Messages.DeliveryStatusNotify message, ZLinkPublishContext context) {
        sessions.publish(message);
    }
}
