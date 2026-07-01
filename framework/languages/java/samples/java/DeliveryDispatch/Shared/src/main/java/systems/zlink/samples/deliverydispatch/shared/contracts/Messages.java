package systems.zlink.samples.deliverydispatch.shared.contracts;

import systems.zlink.framework.handlers.ZLinkPacket;

public final class Messages {
    private Messages() {
    }

    public enum DeliveryStatus {
        Created,
        Assigned,
        Accepted,
        Reassigned,
        PickedUp,
        Delivered,
        Failed
    }

    public record ActorRefSnapshot(String nodeRid, String actorId, long generation) {
    }

    @ZLinkPacket("CreateDeliveryRequest")
    public record CreateDeliveryRequest(
        String deliveryId,
        String customerId,
        String pickupAddress,
        String dropoffAddress) {
    }

    public record CreateDeliveryResponse(String deliveryId, DeliveryStatus status) {
    }

    @ZLinkPacket("SubscribeDelivery")
    public record SubscribeDelivery(String deliveryId) {
    }

    public record SubscribeDeliveryAccepted(String deliveryId) {
    }

    public record DeliveryStatusNotify(
        String deliveryId,
        DeliveryStatus status,
        String courierId,
        String occurredAt) {
    }

    @ZLinkPacket("AssignDelivery")
    public record AssignDelivery(
        String deliveryId,
        String customerId,
        String pickupAddress,
        String dropoffAddress) {
    }

    @ZLinkPacket("BindCourierSession")
    public record BindCourierSession(String courierId, ActorRefSnapshot actor, String sessionRoute) {
        public BindCourierSession(String courierId) {
            this(courierId, null, null);
        }
    }

    public record BindCourierSessionAccepted(String courierId, ActorRefSnapshot actor, String sessionRoute) {
    }

    @ZLinkPacket("BindCourier")
    public record BindCourier(String courierId, String sessionRoute) {
    }

    public record CourierBound(String courierId, ActorRefSnapshot actor, String sessionRoute) {
    }

    @ZLinkPacket("EnsureCourierActor")
    public record EnsureCourierActor(String courierId) {
    }

    public record CourierActorEnsured(String courierId, ActorRefSnapshot actor) {
    }

    @ZLinkPacket("OfferDelivery")
    public record OfferDelivery(
        String courierId,
        String deliveryId,
        String pickupAddress,
        String dropoffAddress) {
    }

    public record OfferDeliveryNotify(
        String courierId,
        String deliveryId,
        String pickupAddress,
        String dropoffAddress) {
    }

    public record OfferDeliveryResult(String deliveryId, String courierId, boolean accepted, String reason) {
    }

    @ZLinkPacket("CourierDecision")
    public record CourierDecision(String deliveryId, String courierId, boolean accepted, String reason) {
    }

    @ZLinkPacket("ReassignDelivery")
    public record ReassignDelivery(
        String deliveryId,
        String previousCourierId,
        String nextCourierId,
        String reason) {
    }

    @ZLinkPacket("DeliveryStatusChanged")
    public record DeliveryStatusChanged(
        String deliveryId,
        DeliveryStatus status,
        String courierId,
        String occurredAt) {
    }

    public record DeliveryStatusAck(String deliveryId, DeliveryStatus status) {
    }

    @ZLinkPacket("EnsureCustomerActor")
    public record EnsureCustomerActor(String customerId) {
    }

    public record CustomerActorEnsured(String customerId, ActorRefSnapshot actorRef) {
    }

    @ZLinkPacket("ServerAssertionRequest")
    public record ServerAssertionRequest(String successfulDeliveryId, String reassignedDeliveryId) {
    }

    public record ServerAssertionResponse(boolean passed, String[] evidence) {
    }
}
