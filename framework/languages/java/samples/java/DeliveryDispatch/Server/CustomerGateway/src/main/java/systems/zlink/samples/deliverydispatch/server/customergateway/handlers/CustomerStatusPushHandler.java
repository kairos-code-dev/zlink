package systems.zlink.samples.deliverydispatch.server.customergateway.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.customergateway.CustomerActorDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("customer-route")
public final class CustomerStatusPushHandler
    implements ZLinkRequestHandler<Messages.DeliveryStatusChanged, Messages.DeliveryStatusAck> {
    private final CustomerActorDirectory customers;

    public CustomerStatusPushHandler(CustomerActorDirectory customers) {
        this.customers = customers;
    }

    @Override
    public Messages.DeliveryStatusAck handle(
        Messages.DeliveryStatusChanged request,
        ZLinkRequestContext context) {
        customers.push(request);
        return new Messages.DeliveryStatusAck(request.deliveryId(), request.status());
    }
}
