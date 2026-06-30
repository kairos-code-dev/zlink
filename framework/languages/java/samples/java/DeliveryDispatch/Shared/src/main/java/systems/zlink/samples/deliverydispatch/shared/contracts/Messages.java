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

    @ZLinkPacket("CreateDeliveryReq")
    public record CreateDeliveryReq(
        String deliveryId,
        String customerId,
        String pickupAddress,
        String dropoffAddress) {
    }

    public record CreateDeliveryRes(String deliveryId) {
    }

    @ZLinkPacket("AssignDelivery")
    public record AssignDelivery(
        String deliveryId,
        String customerId,
        String pickupAddress,
        String dropoffAddress) {
    }

    public record AssignDeliveryRes(String deliveryId, String courierId) {
    }

    @ZLinkPacket("OfferDelivery")
    public record OfferDelivery(
        String deliveryId,
        String pickupAddress,
        String dropoffAddress) {
    }

    public record OfferDeliveryRes(
        String deliveryId,
        String courierId,
        boolean accepted,
        String reason) {
    }

    @ZLinkPacket("DeliveryStatusReq")
    public record DeliveryStatusReq(
        String deliveryId,
        String status,
        String courierId,
        long occurredAtUnixMs) {
    }

    public record DeliveryStatusRes(String deliveryId, String status) {
    }

    @ZLinkPacket("EnsureCustomerActorReq")
    public record EnsureCustomerActorReq(String customerId) {
    }

    public record ActorRefSnapshot(byte[] nodeRid, String actorId, long generation) {
    }

    public record EnsureCustomerActorRes(String customerId, ActorRefSnapshot actor) {
    }

    @ZLinkPacket("SubscribeCustomerToDeliveryReq")
    public record SubscribeCustomerToDeliveryReq(String customerId, String deliveryId) {
    }

    public record SubscribeCustomerToDeliveryRes(String customerId, String deliveryId) {
    }

    public record DeliverySpotCreateReq(String deliveryId) {
    }

    public record DeliverySpotCreateReqd(String deliveryId) {
    }

    public record DeliverySpotJoinReq(String deliveryId, String customerId) {
    }

    public record DeliverySpotJoinReqed(String deliveryId, String customerId) {
    }

    @ZLinkPacket("SubscribeDeliveryReq")
    public record SubscribeDeliveryReq(String deliveryId) {
    }

    @ZLinkPacket("SubscribeDeliveryReqRes")
    public record SubscribeDeliveryReqRes(String deliveryId) {
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
