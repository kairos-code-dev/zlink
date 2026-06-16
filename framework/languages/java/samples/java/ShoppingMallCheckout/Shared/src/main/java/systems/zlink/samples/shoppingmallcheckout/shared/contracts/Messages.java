package systems.zlink.samples.shoppingmallcheckout.shared.contracts;

import java.util.List;
import systems.zlink.framework.handlers.ZLinkPacket;

/**
 * Wire contracts for the ShoppingMallCheckout sample.
 *
 * <p>The sample mirrors the .NET ShoppingMall checkout scenario over ZLink
 * client/server channels with a JSON codec. {@code CommerceApi} owns the
 * HTTP-style API surface, idempotency lookup, and projection reads while the
 * {@code OrderWorkflow} role owns event-sourced order state transitions.
 */
public final class Messages {
    private Messages() {
    }

    /** Order projection status values shared by both server roles and the client. */
    public static final class OrderStatuses {
        public static final String Created = "Created";
        public static final String InventoryReserved = "InventoryReserved";
        public static final String PaymentAuthorized = "PaymentAuthorized";
        public static final String Confirmed = "Confirmed";
        public static final String Failed = "Failed";

        private OrderStatuses() {
        }
    }

    /** A single ordered line item. */
    public record OrderLineInput(String sku, int quantity) {
    }

    /** Current order projection read from the read-model store. */
    public record OrderState(
        String orderId,
        String status,
        String shippingAddressId,
        String reservationId,
        String paymentId,
        String reason,
        Double amount,
        String currency,
        long updatedAtUnixMs) {
    }

    // ----- client HTTP-style contracts -----

    @ZLinkPacket("StartOrderReq")
    public record StartOrderReq(
        String cartId,
        String shippingAddressId,
        String paymentMethodId,
        String idempotencyKey) {
    }

    public record StartOrderRes(String orderId, String status) {
    }

    @ZLinkPacket("GetOrderStateReq")
    public record GetOrderStateReq(String orderId) {
    }

    public record GetOrderStateRes(OrderState state) {
    }

    @ZLinkPacket("DeleteProjectionReq")
    public record DeleteProjectionReq(String orderId) {
    }

    public record DeleteProjectionRes(boolean deleted) {
    }

    @ZLinkPacket("RebuildProjectionApiReq")
    public record RebuildProjectionApiReq(String orderId) {
    }

    public record RebuildProjectionApiRes(OrderState state) {
    }

    @ZLinkPacket("CreatePendingMappingReq")
    public record CreatePendingMappingReq(
        String idempotencyKey,
        String orderId,
        String ownerInstanceId) {
    }

    public record CreatePendingMappingRes(boolean created) {
    }

    @ZLinkPacket("ServerAssertionReq")
    public record ServerAssertionReq(
        String successfulOrderId,
        String pendingRecoveredOrderId,
        String inventoryFailureOrderId,
        String paymentFailureOrderId,
        String scaleOutOrderId) {
    }

    public record ServerAssertionRes(boolean passed, List<String> evidence) {
    }

    // ----- workflow command contracts (CommerceApi -> OrderWorkflow) -----

    @ZLinkPacket("StartOrderWorkflowReq")
    public record StartOrderWorkflowReq(
        String orderId,
        String cartId,
        String shippingAddressId,
        String paymentMethodId,
        String idempotencyKey,
        List<OrderLineInput> lines,
        double amount,
        String currency) {
    }

    public record StartOrderWorkflowRes(OrderState state) {
    }

    @ZLinkPacket("ContinueOrderWorkflowReq")
    public record ContinueOrderWorkflowReq(String orderId) {
    }

    public record ContinueOrderWorkflowRes(OrderState state) {
    }

    @ZLinkPacket("RebuildOrderProjectionReq")
    public record RebuildOrderProjectionReq(String orderId) {
    }

    public record RebuildOrderProjectionRes(OrderState state) {
    }

    // ----- self-check seed contracts -----

    public record CartSeed(String cartId, List<OrderLineInput> lines, double amount, String currency) {
    }

    public record InventorySeed(String sku, int availableQuantity) {
    }

    public record PaymentMethodSeed(String paymentMethodId, boolean shouldAuthorize, String failureReason) {
    }
}
