package systems.zlink.samples.shoppingmallcheckout.server.orderworkflow.domain;

import com.fasterxml.jackson.databind.JsonNode;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages;
import systems.zlink.samples.shoppingmallcheckout.shared.contracts.Messages.OrderState;

/**
 * Pure projection folder: applies a stored event to the running order state.
 *
 * <p>The projection is rebuildable from the event stream alone, which is what
 * the projection-rebuild self-check exercises.
 */
public final class OrderProjection {
    private OrderProjection() {
    }

    public static OrderState apply(OrderState current, String eventType, JsonNode payload) {
        long ts = payload.path("createdAtUnixMs").asLong(
            payload.path("confirmedAtUnixMs").asLong(
                payload.path("failedAtUnixMs").asLong(System.currentTimeMillis())));
        StateBuilder result = switch (eventType) {
            case OrderEvents.OrderStarted -> new StateBuilder(new OrderState(
                payload.path("orderId").asText(),
                Messages.OrderStatuses.Created,
                payload.path("shippingAddressId").asText(null),
                null,
                null,
                null,
                payload.has("amount") ? payload.path("amount").asDouble() : null,
                payload.path("currency").asText(null),
                ts));
            case OrderEvents.InventoryReserved -> require(current).withStatus(
                Messages.OrderStatuses.InventoryReserved, ts)
                .withReservationId(payload.path("reservationId").asText(null));
            case OrderEvents.InventoryReservationFailed -> require(current).withStatus(
                Messages.OrderStatuses.Failed, ts)
                .withReason(payload.path("reason").asText(null));
            case OrderEvents.PaymentAuthorized -> require(current).withStatus(
                Messages.OrderStatuses.PaymentAuthorized, ts)
                .withPaymentId(payload.path("paymentId").asText(null));
            case OrderEvents.PaymentFailed -> require(current).withStatus(
                Messages.OrderStatuses.Failed, ts)
                .withReason(payload.path("reason").asText(null));
            case OrderEvents.InventoryReleased -> require(current).withReason(
                payload.path("reason").asText(null)).touch(ts);
            case OrderEvents.OrderConfirmed -> require(current).withStatus(
                Messages.OrderStatuses.Confirmed, ts);
            case OrderEvents.OrderFailed -> require(current).withStatus(
                Messages.OrderStatuses.Failed, ts)
                .withReason(payload.path("reason").asText(null));
            default -> require(current);
        };
        return result.state();
    }

    private static StateBuilder require(OrderState current) {
        if (current == null) {
            throw new IllegalStateException("Cannot apply order event before OrderStartedEvent.");
        }
        return new StateBuilder(current);
    }

    private record StateBuilder(OrderState state) {
        StateBuilder withStatus(String status, long ts) {
            return new StateBuilder(new OrderState(
                state.orderId(), status, state.shippingAddressId(), state.reservationId(),
                state.paymentId(), state.reason(), state.amount(), state.currency(), ts));
        }

        StateBuilder withReservationId(String reservationId) {
            return new StateBuilder(new OrderState(
                state.orderId(), state.status(), state.shippingAddressId(), reservationId,
                state.paymentId(), state.reason(), state.amount(), state.currency(), state.updatedAtUnixMs()));
        }

        StateBuilder withPaymentId(String paymentId) {
            return new StateBuilder(new OrderState(
                state.orderId(), state.status(), state.shippingAddressId(), state.reservationId(),
                paymentId, state.reason(), state.amount(), state.currency(), state.updatedAtUnixMs()));
        }

        StateBuilder withReason(String reason) {
            return new StateBuilder(new OrderState(
                state.orderId(), state.status(), state.shippingAddressId(), state.reservationId(),
                state.paymentId(), reason, state.amount(), state.currency(), state.updatedAtUnixMs()));
        }

        StateBuilder touch(long ts) {
            return new StateBuilder(new OrderState(
                state.orderId(), state.status(), state.shippingAddressId(), state.reservationId(),
                state.paymentId(), state.reason(), state.amount(), state.currency(), ts));
        }
    }
}
