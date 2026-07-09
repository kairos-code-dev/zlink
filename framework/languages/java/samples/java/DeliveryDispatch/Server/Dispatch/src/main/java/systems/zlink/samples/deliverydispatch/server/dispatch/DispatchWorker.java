package systems.zlink.samples.deliverydispatch.server.dispatch;

import java.time.Instant;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.locations.SpotRef;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class DispatchWorker {
    private final ZLinkClient channels;
    private final ZLinkRouteClient routes;

    public DispatchWorker(ZLinkClient channels, ZLinkRouteClient routes) {
        this.channels = channels;
        this.routes = routes;
    }

    public void dispatch(Messages.AssignDelivery request) {
        if ("delivery-reassign".equals(request.deliveryId())) {
            dispatchReassigned(request);
            return;
        }
        dispatchSuccessful(request);
    }

    public Messages.ServerAssertionResponse assertServerEvidence(Messages.ServerAssertionRequest request) {
        return channels
            .requestToChannel(SampleNames.TrackingChannel, request)
            .await(Messages.ServerAssertionResponse.class);
    }

    private void dispatchSuccessful(Messages.AssignDelivery request) {
        publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.Assigned, "courier-a");
        Messages.OfferDeliveryResult offered = offer(request, "courier-a");
        if (!offered.accepted()) {
            publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.Failed, "courier-a");
            return;
        }
        publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.Accepted, "courier-a");
        publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.PickedUp, "courier-a");
        publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.Delivered, "courier-a");
    }

    private void dispatchReassigned(Messages.AssignDelivery request) {
        publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.Assigned, "courier-a");
        Messages.OfferDeliveryResult first = offer(request, "courier-a");
        if (first.accepted()) {
            publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.Accepted, "courier-a");
            publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.Delivered, "courier-a");
            return;
        }
        publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.Reassigned, "courier-b");
        Messages.OfferDeliveryResult second = offer(request, "courier-b");
        if (!second.accepted()) {
            publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.Failed, "courier-b");
            return;
        }
        publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.Accepted, "courier-b");
        publishStatus(request.deliveryId(), request.customerId(), Messages.DeliveryStatus.Delivered, "courier-b");
    }

    private Messages.OfferDeliveryResult offer(Messages.AssignDelivery request, String courierId) {
        SpotRef address = courierAddress(courierId);
        Messages.CourierActorFound found = routes
            .requestToSpot(SampleNames.CourierSpotDiscovery, address, new Messages.FindCourierActor(courierId))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.CourierActorFound.class);
        if (found.actor() == null) {
            return new Messages.OfferDeliveryResult(
                request.deliveryId(),
                courierId,
                false,
                "courier actor is not bound: " + courierId);
        }
        return routes
            .requestToSpot(
                SampleNames.CourierSpotDiscovery,
                address,
                new Messages.OfferDelivery(
                    courierId,
                    request.deliveryId(),
                    request.pickupAddress(),
                    request.dropoffAddress()))
            .timeout(SampleTimings.OfferRequestTimeout)
            .await(Messages.OfferDeliveryResult.class);
    }

    private SpotRef courierAddress(String courierId) {
        RoutingId nodeRid = RoutingId.from(SampleTopology.courierPlacement(courierId));
        return new SpotRef(SampleNames.CourierSpotDiscovery, nodeRid, nodeRid);
    }

    private void publishStatus(
        String deliveryId,
        String customerId,
        Messages.DeliveryStatus status,
        String courierId) {
        channels.requestToChannel(
                SampleNames.TrackingChannel,
                new Messages.DeliveryStatusChanged(
                    deliveryId,
                    customerId,
                    status,
                    courierId,
                    Instant.now().toString()))
            .await(Messages.DeliveryStatusAck.class);
    }
}
