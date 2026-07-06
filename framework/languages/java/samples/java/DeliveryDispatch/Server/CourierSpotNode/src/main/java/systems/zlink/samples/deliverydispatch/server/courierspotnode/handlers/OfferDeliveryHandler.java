package systems.zlink.samples.deliverydispatch.server.courierspotnode.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.ActorDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("courier-actor-node")
public final class OfferDeliveryHandler
    implements ZLinkRequestHandler<Messages.OfferDelivery, Messages.OfferDeliveryResult> {
    private final ActorDirectory directory;

    public OfferDeliveryHandler(ActorDirectory directory) {
        this.directory = directory;
    }

    @Override
    public Messages.OfferDeliveryResult handle(
        Messages.OfferDelivery request,
        ZLinkRequestContext context) {
        return directory.require(request.courierId()).offer(request);
    }
}
