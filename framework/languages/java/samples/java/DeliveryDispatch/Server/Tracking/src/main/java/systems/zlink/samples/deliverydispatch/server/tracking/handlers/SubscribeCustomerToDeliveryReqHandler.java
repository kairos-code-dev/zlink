package systems.zlink.samples.deliverydispatch.server.tracking.handlers;

import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.tracking.spots.deliverytrackingspot.DeliveryTrackingSpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("tracking")
public final class SubscribeCustomerToDeliveryReqHandler
    implements ZLinkRequestHandler<
        Messages.SubscribeCustomerToDeliveryReq,
        Messages.SubscribeCustomerToDeliveryRes> {
    private final ZLinkActorManager actors;
    private final ZLinkSpotManager spots;
    private final ObjectMapper json;

    public SubscribeCustomerToDeliveryReqHandler(
        ZLinkActorManager actors,
        ZLinkSpotManager spots,
        ObjectMapper json) {
        this.actors = actors;
        this.spots = spots;
        this.json = json;
    }

    @Override
    public Messages.SubscribeCustomerToDeliveryRes handle(
        Messages.SubscribeCustomerToDeliveryReq request,
        ZLinkRequestContext context) {
        ZLinkAwait.await(spots.getOrCreate(
            DeliveryTrackingSpot.class,
            RoutingId.from(request.deliveryId()),
            new Messages.DeliverySpotCreateReq(request.deliveryId())));
        var actor = ZLinkAwait.await(
            actors.getOrCreate(request.customerId(), SampleNames.CustomerActorType));
        if (actor == null) {
            throw new IllegalStateException("Customer actor was not created.");
        }
        return new Messages.SubscribeCustomerToDeliveryRes(request.customerId(), request.deliveryId());
    }

}
