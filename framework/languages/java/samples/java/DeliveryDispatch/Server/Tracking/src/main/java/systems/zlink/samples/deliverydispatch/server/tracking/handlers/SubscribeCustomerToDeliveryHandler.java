package systems.zlink.samples.deliverydispatch.server.tracking.handlers;

import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.actors.ZLinkActorGateway;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.tracking.spots.DeliveryTrackingSpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

@ZLinkHandlerGroup("tracking")
public final class SubscribeCustomerToDeliveryHandler
    implements ZLinkRequestHandler<
        Messages.SubscribeCustomerToDelivery,
        Messages.CustomerDeliverySubscribed> {
    private final ZLinkActorManager actors;
    private final ZLinkActorGateway actorGateway;
    private final ZLinkSpotManager spots;
    private final ObjectMapper json;

    public SubscribeCustomerToDeliveryHandler(
        ZLinkActorManager actors,
        ZLinkActorGateway actorGateway,
        ZLinkSpotManager spots,
        ObjectMapper json) {
        this.actors = actors;
        this.actorGateway = actorGateway;
        this.spots = spots;
        this.json = json;
    }

    @Override
    public Messages.CustomerDeliverySubscribed handle(
        Messages.SubscribeCustomerToDelivery request,
        ZLinkRequestContext context) {
        ZLinkAwait.await(spots.getOrCreate(
            DeliveryTrackingSpot.class,
            RoutingId.from(request.deliveryId()),
            new Messages.DeliverySpotCreate(request.deliveryId())));
        var actor = ZLinkAwait.await(
            actors.getOrCreate(request.customerId(), SampleNames.CustomerActorType));
        actorGateway
            .joinSpot(
                actor,
                RoutingId.from(request.deliveryId()),
                new Messages.DeliverySpotJoin(request.deliveryId(), request.customerId()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.DeliverySpotJoined.class);
        return new Messages.CustomerDeliverySubscribed(request.customerId(), request.deliveryId());
    }

}
