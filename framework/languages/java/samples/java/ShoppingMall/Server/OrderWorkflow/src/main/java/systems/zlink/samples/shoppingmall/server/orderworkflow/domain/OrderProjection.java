package systems.zlink.samples.shoppingmall.server.orderworkflow.domain;

/**
 * Pure projection folder: applies a stored event to the running order state.
 *
 * <p>The projection is rebuildable from the event stream alone, which is what
 * the projection-rebuild self-check exercises.
 */
public final class OrderProjection {
    private OrderProjection() {
    }

    public static State apply(State current, OrderAggregate.StoredOrderEvent event) {
        long ts = event.createdAtUnixMs();
        Object payload = event.payload();
        StateBuilder result = switch (event.eventType()) {
            case OrderEvents.OrderStarted -> new StateBuilder(new State(
                ((OrderEvents.OrderStartedEvent) payload).orderId(),
                OrderStatus.Created,
                ((OrderEvents.OrderStartedEvent) payload).shippingAddressId(),
                null,
                null,
                null,
                ((OrderEvents.OrderStartedEvent) payload).amount(),
                ((OrderEvents.OrderStartedEvent) payload).currency(),
                ts));
            case OrderEvents.InventoryReserved -> require(current).withStatus(
                OrderStatus.InventoryReserved, ts)
                .withReservationId(((OrderEvents.InventoryReservedEvent) payload).reservationId());
            case OrderEvents.InventoryReservationFailed -> require(current).withStatus(
                OrderStatus.Failed, ts)
                .withReason(((OrderEvents.InventoryReservationFailedEvent) payload).reason());
            case OrderEvents.PaymentAuthorized -> require(current).withStatus(
                OrderStatus.PaymentAuthorized, ts)
                .withPaymentId(((OrderEvents.PaymentAuthorizedEvent) payload).paymentId());
            case OrderEvents.PaymentFailed -> require(current).withStatus(
                OrderStatus.Failed, ts)
                .withReason(((OrderEvents.PaymentFailedEvent) payload).reason());
            case OrderEvents.InventoryReleased -> require(current).withReason(
                ((OrderEvents.InventoryReleasedEvent) payload).reason()).touch(ts);
            case OrderEvents.OrderConfirmed -> require(current).withStatus(
                OrderStatus.Confirmed, ts);
            case OrderEvents.OrderFailed -> require(current).withStatus(
                OrderStatus.Failed, ts)
                .withReason(((OrderEvents.OrderFailedEvent) payload).reason());
            default -> require(current);
        };
        return result.state();
    }

    private static StateBuilder require(State current) {
        if (current == null) {
            throw new IllegalStateException("Cannot apply order event before OrderStartedEvent.");
        }
        return new StateBuilder(current);
    }

    public record State(
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

    private record StateBuilder(State state) {
        StateBuilder withStatus(String status, long ts) {
            return new StateBuilder(new State(
                state.orderId(), status, state.shippingAddressId(), state.reservationId(),
                state.paymentId(), state.reason(), state.amount(), state.currency(), ts));
        }

        StateBuilder withReservationId(String reservationId) {
            return new StateBuilder(new State(
                state.orderId(), state.status(), state.shippingAddressId(), reservationId,
                state.paymentId(), state.reason(), state.amount(), state.currency(), state.updatedAtUnixMs()));
        }

        StateBuilder withPaymentId(String paymentId) {
            return new StateBuilder(new State(
                state.orderId(), state.status(), state.shippingAddressId(), state.reservationId(),
                paymentId, state.reason(), state.amount(), state.currency(), state.updatedAtUnixMs()));
        }

        StateBuilder withReason(String reason) {
            return new StateBuilder(new State(
                state.orderId(), state.status(), state.shippingAddressId(), state.reservationId(),
                state.paymentId(), reason, state.amount(), state.currency(), state.updatedAtUnixMs()));
        }

        StateBuilder touch(long ts) {
            return new StateBuilder(new State(
                state.orderId(), state.status(), state.shippingAddressId(), state.reservationId(),
                state.paymentId(), state.reason(), state.amount(), state.currency(), ts));
        }
    }
}
