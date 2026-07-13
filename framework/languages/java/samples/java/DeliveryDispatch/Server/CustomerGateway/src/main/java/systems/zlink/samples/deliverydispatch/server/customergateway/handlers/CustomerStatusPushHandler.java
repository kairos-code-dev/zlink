package systems.zlink.samples.deliverydispatch.server.customergateway.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.deliverydispatch.server.customergateway.CustomerActorDirectory;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

@ZLinkHandlerGroup("customer-route")
public final class CustomerStatusPushHandler
    implements ZLinkRequestHandler<Messages.DeliveryStatusChangedReq, Messages.DeliveryStatusChangedRes> {
    private final CustomerActorDirectory customers;

    public CustomerStatusPushHandler(CustomerActorDirectory customers) {
        this.customers = customers;
    }

    @Override
    public CompletionStage<Messages.DeliveryStatusChangedRes> handle(
        Messages.DeliveryStatusChangedReq request,
        ZLinkRequestContext context) {
        customers.push(request);
        return CompletableFuture.completedFuture(
            new Messages.DeliveryStatusChangedRes(request.deliveryId(), request.status()));
    }
}
