package systems.zlink.samples.deliverydispatch.server.dispatch;

import java.time.Instant;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTimings;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleTopology;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class DispatchWorker {
    private final ZLinkClient channels;
    private final ZLinkRouteClient routes;
    private final SpotHandleResolver spotHandles;

    public DispatchWorker(
        ZLinkClient channels,
        ZLinkRouteClient routes,
        SpotHandleResolver spotHandles) {
        this.channels = channels;
        this.routes = routes;
        this.spotHandles = spotHandles;
    }

    public CompletionStage<Void> dispatch(Messages.AssignDeliveryMsg request) {
        return "delivery-reassign".equals(request.deliveryId())
            ? dispatchReassigned(request)
            : dispatchSuccessful(request);
    }

    public CompletionStage<Messages.ServerAssertionResponse> assertServerEvidence(
        Messages.ServerAssertionRequest request) {
        return channels
            .requestToChannel(SampleNames.TrackingChannel, request)
            .submit(Messages.ServerAssertionResponse.class);
    }

    private CompletionStage<Void> dispatchSuccessful(Messages.AssignDeliveryMsg request) {
        return publishStatus(request, Messages.DeliveryStatus.Assigned, "courier-a")
            .thenCompose(ignored -> offer(request, "courier-a"))
            .thenCompose(offered -> !offered.accepted()
                ? publishStatus(request, Messages.DeliveryStatus.Failed, "courier-a")
                : publishStatus(request, Messages.DeliveryStatus.Accepted, "courier-a")
                    .thenCompose(ignored -> publishStatus(
                        request, Messages.DeliveryStatus.PickedUp, "courier-a"))
                    .thenCompose(ignored -> publishStatus(
                        request, Messages.DeliveryStatus.Delivered, "courier-a")));
    }

    private CompletionStage<Void> dispatchReassigned(Messages.AssignDeliveryMsg request) {
        return publishStatus(request, Messages.DeliveryStatus.Assigned, "courier-a")
            .thenCompose(ignored -> offer(request, "courier-a"))
            .thenCompose(first -> first.accepted()
                ? publishStatus(request, Messages.DeliveryStatus.Accepted, "courier-a")
                    .thenCompose(ignored -> publishStatus(
                        request, Messages.DeliveryStatus.Delivered, "courier-a"))
                : publishStatus(request, Messages.DeliveryStatus.Reassigned, "courier-b")
                    .thenCompose(ignored -> offer(request, "courier-b"))
                    .thenCompose(second -> !second.accepted()
                        ? publishStatus(request, Messages.DeliveryStatus.Failed, "courier-b")
                        : publishStatus(request, Messages.DeliveryStatus.Accepted, "courier-b")
                            .thenCompose(ignored -> publishStatus(
                                request, Messages.DeliveryStatus.Delivered, "courier-b"))));
    }

    private CompletionStage<Messages.OfferDeliveryRes> offer(
        Messages.AssignDeliveryMsg request,
        String courierId) {
        return courierAddress(courierId).thenCompose(address -> routes
                .requestToSpot(SampleNames.CourierSpotDiscovery, address,
                    new Messages.FindCourierActorReq(courierId))
                .timeout(SampleTimings.RequestTimeout)
                .submit(Messages.FindCourierActorRes.class)
                .thenCompose(found -> found.actor() == null
                    ? CompletableFuture.completedFuture(new Messages.OfferDeliveryRes(
                        request.deliveryId(), courierId, false,
                        "courier actor is not bound: " + courierId))
                    : routes.requestToSpot(
                            SampleNames.CourierSpotDiscovery,
                            address,
                            new Messages.OfferDeliveryReq(courierId, request.deliveryId(),
                                request.pickupAddress(), request.dropoffAddress()))
                        .timeout(SampleTimings.OfferRequestTimeout)
                        .submit(Messages.OfferDeliveryRes.class)));
    }

    private CompletionStage<SpotHandle> courierAddress(String courierId) {
        RoutingId nodeRid = RoutingId.from(SampleTopology.courierPlacement(courierId));
        return spotHandles.resolveSpotHandle(nodeRid).thenApply(found -> found.orElseThrow(() ->
            new IllegalStateException("courier Spot is not registered: " + nodeRid)));
    }

    private CompletionStage<Void> publishStatus(
        Messages.AssignDeliveryMsg request,
        Messages.DeliveryStatus status,
        String courierId) {
        return channels.requestToChannel(
                SampleNames.TrackingChannel,
                new Messages.DeliveryStatusChangedReq(
                    request.deliveryId(),
                    status,
                    courierId,
                    Instant.now().toString()))
            .submit(Messages.DeliveryStatusChangedRes.class)
            .thenApply(ignored -> null);
    }
}
