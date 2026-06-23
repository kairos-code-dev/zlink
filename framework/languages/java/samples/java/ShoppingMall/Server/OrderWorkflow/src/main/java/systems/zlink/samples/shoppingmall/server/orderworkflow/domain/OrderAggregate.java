package systems.zlink.samples.shoppingmall.server.orderworkflow.domain;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import com.fasterxml.jackson.databind.JsonNode;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.OrderLineInput;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.StartOrderWorkflowReq;

/**
 * Order aggregate rebuilt from the event stream. Owns the order status state
 * machine, command dedupe, and compensation rules. Knows nothing about the
 * framework, the store implementation, or transport.
 */
public final class OrderAggregate {
    private final String orderId;
    private final Set<String> processedCommands = new HashSet<>();
    private String status;
    private String reservationId;
    private List<OrderLineInput> lines = new ArrayList<>();

    private OrderAggregate(String orderId) {
        this.orderId = orderId;
    }

    public static OrderAggregate rehydrate(
        String orderId,
        List<StoredOrderEvent> stored,
        OrderEventCodec codec) {
        OrderAggregate aggregate = new OrderAggregate(orderId);
        for (StoredOrderEvent event : stored) {
            aggregate.apply(event, codec);
        }
        return aggregate;
    }

    private void apply(StoredOrderEvent event, OrderEventCodec codec) {
        switch (event.eventType()) {
            case OrderEvents.OrderStarted -> {
                OrderEvents.OrderStartedEvent started =
                    codec.decode(event.payload(), OrderEvents.OrderStartedEvent.class);
                this.status = Messages.OrderStatuses.Created;
                this.lines = started.lines();
                if (started.sourceCommandId() != null) {
                    processedCommands.add(started.sourceCommandId());
                }
            }
            case OrderEvents.InventoryReserved -> {
                this.status = Messages.OrderStatuses.InventoryReserved;
                this.reservationId = event.payload().path("reservationId").asText(null);
            }
            case OrderEvents.InventoryReservationFailed -> this.status = Messages.OrderStatuses.Failed;
            case OrderEvents.PaymentAuthorized -> this.status = Messages.OrderStatuses.PaymentAuthorized;
            case OrderEvents.PaymentFailed -> this.status = Messages.OrderStatuses.Failed;
            case OrderEvents.OrderConfirmed -> this.status = Messages.OrderStatuses.Confirmed;
            case OrderEvents.OrderFailed -> this.status = Messages.OrderStatuses.Failed;
            default -> {
            }
        }
    }

    public boolean hasStarted() {
        return status != null;
    }

    public boolean isTerminal() {
        return Messages.OrderStatuses.Confirmed.equals(status)
            || Messages.OrderStatuses.Failed.equals(status);
    }

    public String status() {
        return status;
    }

    public List<OrderLineInput> lines() {
        return lines;
    }

    public boolean hasProcessedCommand(String sourceCommandId) {
        return processedCommands.contains(sourceCommandId);
    }

    public List<Object> start(StartOrderWorkflowReq command, String eventId, long now) {
        if (hasStarted()) {
            return List.of();
        }
        return List.of(new OrderEvents.OrderStartedEvent(
            eventId,
            command.idempotencyKey(),
            orderId,
            command.cartId(),
            command.shippingAddressId(),
            command.lines(),
            command.amount(),
            command.currency(),
            now));
    }

    public List<Object> applyInventoryResult(
        InventoryReservationResult result,
        String reservedEventId,
        String failedEventId,
        long now) {
        if (isTerminal() || !Messages.OrderStatuses.Created.equals(status)) {
            return List.of();
        }
        if (!result.accepted()) {
            String reason = result.reason() == null ? "inventory unavailable" : result.reason();
            return List.of(
                new OrderEvents.InventoryReservationFailedEvent(failedEventId, orderId, reason, now),
                new OrderEvents.OrderFailedEvent(failedEventId + "-failed", orderId, reason, now));
        }
        return List.of(new OrderEvents.InventoryReservedEvent(
            reservedEventId, orderId, result.reservationId(), now));
    }

    public List<Object> applyPaymentResult(
        PaymentAuthorizationResult result,
        String paymentEventId,
        String releaseEventId,
        String failedEventId,
        long now) {
        if (isTerminal() || !Messages.OrderStatuses.InventoryReserved.equals(status)) {
            return List.of();
        }
        if (!result.accepted()) {
            String reason = result.reason() == null ? "payment failed" : result.reason();
            return List.of(
                new OrderEvents.PaymentFailedEvent(paymentEventId, orderId, reason, now),
                new OrderEvents.InventoryReleasedEvent(releaseEventId, orderId, reservationId, reason, now),
                new OrderEvents.OrderFailedEvent(failedEventId, orderId, reason, now));
        }
        return List.of(new OrderEvents.PaymentAuthorizedEvent(
            paymentEventId, orderId, result.paymentId(), now));
    }

    public List<Object> confirm(String eventId, long now) {
        if (isTerminal() || !Messages.OrderStatuses.PaymentAuthorized.equals(status)) {
            return List.of();
        }
        return List.of(new OrderEvents.OrderConfirmedEvent(eventId, orderId, now));
    }

    /** Decoder/encoder bridge so the aggregate stays free of Jackson types. */
    public interface OrderEventCodec {
        <T> T decode(JsonNode payload, Class<T> type);
    }

    public String reservationId() {
        return reservationId;
    }

    public record StoredOrderEvent(
        String eventId,
        String sourceCommandId,
        String orderId,
        String eventType,
        JsonNode payload,
        long version,
        long createdAtUnixMs) {
    }

    public record InventoryReservationResult(boolean accepted, String reservationId, String reason) {
    }

    public record PaymentAuthorizationResult(boolean accepted, String paymentId, String reason) {
    }
}
