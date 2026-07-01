package systems.zlink.samples.deliverydispatch.server.dispatch;

import java.time.Instant;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class DispatchWorker {
    private final ZLinkClient channels;

    public DispatchWorker(ZLinkClient channels) {
        this.channels = channels;
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
        publishStatus(request.deliveryId(), Messages.DeliveryStatus.Assigned, "courier-a");
        Messages.OfferDeliveryResult offered = offer(request, "courier-a");
        if (!offered.accepted()) {
            publishStatus(request.deliveryId(), Messages.DeliveryStatus.Failed, "courier-a");
            return;
        }
        publishStatus(request.deliveryId(), Messages.DeliveryStatus.Accepted, "courier-a");
        publishStatus(request.deliveryId(), Messages.DeliveryStatus.PickedUp, "courier-a");
        publishStatus(request.deliveryId(), Messages.DeliveryStatus.Delivered, "courier-a");
    }

    private void dispatchReassigned(Messages.AssignDelivery request) {
        publishStatus(request.deliveryId(), Messages.DeliveryStatus.Assigned, "courier-a");
        Messages.OfferDeliveryResult first = offer(request, "courier-a");
        if (first.accepted()) {
            publishStatus(request.deliveryId(), Messages.DeliveryStatus.Accepted, "courier-a");
            publishStatus(request.deliveryId(), Messages.DeliveryStatus.Delivered, "courier-a");
            return;
        }
        publishStatus(request.deliveryId(), Messages.DeliveryStatus.Reassigned, "courier-b");
        Messages.OfferDeliveryResult second = offer(request, "courier-b");
        if (!second.accepted()) {
            publishStatus(request.deliveryId(), Messages.DeliveryStatus.Failed, "courier-b");
            return;
        }
        publishStatus(request.deliveryId(), Messages.DeliveryStatus.Accepted, "courier-b");
        publishStatus(request.deliveryId(), Messages.DeliveryStatus.Delivered, "courier-b");
    }

    private Messages.OfferDeliveryResult offer(Messages.AssignDelivery request, String courierId) {
        return channels
            .requestToChannel(
                SampleNames.CourierChannel,
                new Messages.OfferDelivery(
                    courierId,
                    request.deliveryId(),
                    request.pickupAddress(),
                    request.dropoffAddress()))
            .await(Messages.OfferDeliveryResult.class);
    }

    private void publishStatus(
        String deliveryId,
        Messages.DeliveryStatus status,
        String courierId) {
        channels.requestToChannel(
                SampleNames.TrackingChannel,
                new Messages.DeliveryStatusChanged(
                    deliveryId,
                    status,
                    courierId,
                    Instant.now().toString()))
            .await(Messages.DeliveryStatusAck.class);
    }
}
