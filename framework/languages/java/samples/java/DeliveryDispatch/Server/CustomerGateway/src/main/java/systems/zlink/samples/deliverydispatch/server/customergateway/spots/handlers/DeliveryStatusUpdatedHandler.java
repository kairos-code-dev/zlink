package systems.zlink.samples.deliverydispatch.server.customergateway.spots.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.samples.deliverydispatch.server.customergateway.CustomerActor;
import systems.zlink.samples.deliverydispatch.server.customergateway.spots.CustomerEntrySpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class DeliveryStatusUpdatedHandler
    implements ZLinkEntrySpotActorSendHandler<
        CustomerEntrySpot,
        CustomerActor,
        Messages.DeliveryStatusUpdatedMsg> {
    @Override
    public void handle(
        CustomerEntrySpot entrySpot,
        CustomerActor actor,
        ZLinkSpotActorSendContext context,
        Messages.DeliveryStatusUpdatedMsg message,
        CancellationToken cancellationToken) {
        actor.push(new Messages.DeliveryStatusNotify(
            message.deliveryId(),
            message.status(),
            message.courierId(),
            message.occurredAt()));
    }
}
