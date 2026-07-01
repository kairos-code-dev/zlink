package systems.zlink.samples.deliverydispatch.server.customergateway.spots.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.deliverydispatch.server.customergateway.CustomerActor;
import systems.zlink.samples.deliverydispatch.server.customergateway.spots.CustomerEntrySpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class SubscribeDeliveryActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        CustomerEntrySpot,
        CustomerActor,
        Messages.SubscribeDelivery,
        Messages.SubscribeDeliveryAccepted> {
    @Override
    public Messages.SubscribeDeliveryAccepted handle(
        CustomerEntrySpot entrySpot,
        CustomerActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.SubscribeDelivery request,
        CancellationToken cancellationToken) {
        return entrySpot.subscribe(actor, request);
    }
}
