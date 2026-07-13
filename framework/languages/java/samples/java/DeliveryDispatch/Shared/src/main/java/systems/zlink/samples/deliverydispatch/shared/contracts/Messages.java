package systems.zlink.samples.deliverydispatch.shared.contracts;

import systems.zlink.framework.actors.ActorRefSnapshot;
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

    @ZLinkPacket("CreateDeliveryReq")
    public record CreateDeliveryReq(
        String deliveryId,
        String customerId,
        String pickupAddress,
        String dropoffAddress) {
    }

    public record CreateDeliveryRes(String deliveryId) {
    }

    @ZLinkPacket("SubscribeDeliveryReq")
    public record SubscribeDeliveryReq(String deliveryId) {
    }

    public record SubscribeDeliveryRes(String deliveryId) {
    }

    public record DeliveryStatusNotify(
        String deliveryId,
        DeliveryStatus status,
        String courierId,
        String occurredAt) {
    }

    @ZLinkPacket("AssignDeliveryMsg")
    public record AssignDeliveryMsg(
        String deliveryId,
        String customerId,
        String pickupAddress,
        String dropoffAddress) {
    }

    @ZLinkPacket("BindCourierSessionReq")
    public record BindCourierSessionReq(String courierId, ActorRefSnapshot actor, String sessionRoute) {
        public BindCourierSessionReq(String courierId) {
            this(courierId, null, null);
        }
    }

    public record BindCourierSessionRes(
        String courierId,
        ActorRefSnapshot actor,
        String sessionRoute) {
    }

    @ZLinkPacket("BindCourierReq")
    public record BindCourierReq(String courierId, String sessionRoute) {
    }

    public record BindCourierRes(String courierId, ActorRefSnapshot actor, String sessionRoute) {
    }

    @ZLinkPacket("FindCourierActorReq")
    public record FindCourierActorReq(String courierId) {
    }

    public record FindCourierActorRes(String courierId, ActorRefSnapshot actor) {
    }

    @ZLinkPacket("EnsureCourierActorReq")
    public record EnsureCourierActorReq(String courierId) {
    }

    public record EnsureCourierActorRes(String courierId, ActorRefSnapshot actor) {
    }

    @ZLinkPacket("OfferDeliveryReq")
    public record OfferDeliveryReq(
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

    public record OfferDeliveryRes(String deliveryId, String courierId, boolean accepted, String reason) {
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

    @ZLinkPacket("DeliveryStatusChangedReq")
    public record DeliveryStatusChangedReq(
        String deliveryId,
        DeliveryStatus status,
        String courierId,
        String occurredAt) {
    }

    @ZLinkPacket("DeliveryStatusUpdatedMsg")
    public record DeliveryStatusUpdatedMsg(
        String deliveryId,
        String customerId,
        DeliveryStatus status,
        String courierId,
        String occurredAt) {
    }

    public record DeliveryStatusChangedRes(String deliveryId, DeliveryStatus status) {
    }

    @ZLinkPacket("FindCustomerActorReq")
    public record FindCustomerActorReq(String customerId) {
    }

    public record FindCustomerActorRes(String customerId, ActorRefSnapshot actorRef) {
    }

    @ZLinkPacket("EnsureCustomerActorReq")
    public record EnsureCustomerActorReq(String customerId) {
    }

    public record EnsureCustomerActorRes(String customerId, ActorRefSnapshot actorRef) {
    }

    @ZLinkPacket("ServerAssertionRequest")
    public record ServerAssertionRequest(String successfulDeliveryId, String reassignedDeliveryId) {
    }

    public record ServerAssertionResponse(boolean passed, String[] evidence) {
    }
}
