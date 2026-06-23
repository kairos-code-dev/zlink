package systems.zlink.samples.shoppingmall.server.orderworkflow.domain;

import java.util.List;

/**
 * Order domain events.
 *
 * <p>Each event type maps to its simple class name; the same names are used as
 * the {@code eventType} discriminator inside the event store and as the
 * sequence the self-check assertion compares against.
 */
public final class OrderEvents {
    private OrderEvents() {
    }

    public static final String OrderStarted = "OrderStartedEvent";
    public static final String InventoryReserved = "InventoryReservedEvent";
    public static final String InventoryReservationFailed = "InventoryReservationFailedEvent";
    public static final String PaymentAuthorized = "PaymentAuthorizedEvent";
    public static final String PaymentFailed = "PaymentFailedEvent";
    public static final String InventoryReleased = "InventoryReleasedEvent";
    public static final String OrderConfirmed = "OrderConfirmedEvent";
    public static final String OrderFailed = "OrderFailedEvent";

    public record OrderLine(String sku, int quantity) {
    }

    public record StartOrderCommand(
        String orderId,
        String cartId,
        String shippingAddressId,
        String paymentMethodId,
        String idempotencyKey,
        List<OrderLine> lines,
        double amount,
        String currency) {
    }

    public record OrderStartedEvent(
        String eventId,
        String sourceCommandId,
        String orderId,
        String cartId,
        String shippingAddressId,
        String paymentMethodId,
        List<OrderLine> lines,
        double amount,
        String currency,
        long createdAtUnixMs) {
    }

    public record InventoryReservedEvent(
        String eventId, String orderId, String reservationId, long createdAtUnixMs) {
    }

    public record InventoryReservationFailedEvent(
        String eventId, String orderId, String reason, long createdAtUnixMs) {
    }

    public record PaymentAuthorizedEvent(
        String eventId, String orderId, String paymentId, long createdAtUnixMs) {
    }

    public record PaymentFailedEvent(
        String eventId, String orderId, String reason, long createdAtUnixMs) {
    }

    public record InventoryReleasedEvent(
        String eventId, String orderId, String reservationId, String reason, long createdAtUnixMs) {
    }

    public record OrderConfirmedEvent(
        String eventId, String orderId, long confirmedAtUnixMs) {
    }

    public record OrderFailedEvent(
        String eventId, String orderId, String reason, long failedAtUnixMs) {
    }
}
