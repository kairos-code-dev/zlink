package systems.zlink.samples.deliverydispatch.shared.contracts;

import java.util.List;
import systems.zlink.framework.handlers.ZLinkPacket;

public final class Messages {
    private Messages() {
    }

    public static final class Status {
        public static final String Created = "Created";
        public static final String Assigned = "Assigned";
        public static final String Accepted = "Accepted";
        public static final String Reassigned = "Reassigned";
        public static final String PickedUp = "PickedUp";
        public static final String Delivered = "Delivered";
        public static final String Failed = "Failed";

        private Status() {
        }
    }

    @ZLinkPacket("CreateDeliveryRequest")
    public record CreateDeliveryRequest(
        String deliveryId,
        String customerId,
        String pickupAddress,
        String dropoffAddress) {
    }

    public record DeliveryCreated(String deliveryId) {
    }

    @ZLinkPacket("AssignDelivery")
    public record AssignDelivery(
        String deliveryId,
        String customerId,
        String pickupAddress,
        String dropoffAddress) {
    }

    public record AssignDeliveryResult(String deliveryId, String courierId) {
    }

    @ZLinkPacket("OfferDelivery")
    public record OfferDelivery(
        String deliveryId,
        String pickupAddress,
        String dropoffAddress) {
    }

    public record OfferDeliveryResult(
        String deliveryId,
        String courierId,
        boolean accepted,
        String reason) {
    }

    @ZLinkPacket("DeliveryStatusChanged")
    public record DeliveryStatusChanged(
        String deliveryId,
        String status,
        String courierId,
        long occurredAtUnixMs) {
    }

    public record DeliveryStatusAck(String deliveryId, String status) {
    }

    @ZLinkPacket("EnsureCustomerActor")
    public record EnsureCustomerActor(String customerId) {
    }

    public record ActorRefSnapshot(byte[] nodeRid, String actorId, long generation) {
    }

    public record CustomerActorEnsured(String customerId, ActorRefSnapshot actor) {
    }

    @ZLinkPacket("SubscribeCustomerToDelivery")
    public record SubscribeCustomerToDelivery(String customerId, String deliveryId) {
    }

    public record CustomerDeliverySubscribed(String customerId, String deliveryId) {
    }

    public record DeliverySpotCreate(String deliveryId) {
    }

    public record DeliverySpotCreated(String deliveryId) {
    }

    public record DeliverySpotJoin(String deliveryId, String customerId) {
    }

    public record DeliverySpotJoined(String deliveryId, String customerId) {
    }

    @ZLinkPacket("SubscribeDelivery")
    public record SubscribeDelivery(String deliveryId) {
    }

    @ZLinkPacket("SubscribeDeliveryAccepted")
    public record SubscribeDeliveryAccepted(String deliveryId) {
    }

    @ZLinkPacket("DeliveryStatusNotify")
    public record DeliveryStatusNotify(
        String deliveryId,
        String status,
        String courierId,
        long occurredAtUnixMs) {
    }

    @ZLinkPacket("ServerAssertionReq")
    public record ServerAssertionReq(
        String successfulDeliveryId,
        String reassignedDeliveryId) {
    }

    public record ServerAssertionRes(boolean passed, List<String> evidence) {
    }
}
