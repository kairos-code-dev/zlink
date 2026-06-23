package systems.zlink.samples.shoppingmall.server.orderworkflow.domain;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import systems.zlink.samples.shoppingmall.server.orderworkflow.domain.OrderEvents.StartOrderCommand;

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
    private String paymentMethodId;
    private double amount;
    private String currency;
    private List<OrderEvents.OrderLine> lines = new ArrayList<>();

    private OrderAggregate(String orderId) {
        this.orderId = orderId;
    }

    public static OrderAggregate rehydrate(
        String orderId,
        List<StoredOrderEvent> stored) {
        OrderAggregate aggregate = new OrderAggregate(orderId);
        for (StoredOrderEvent event : stored) {
            aggregate.apply(event);
        }
        return aggregate;
    }

    private void apply(StoredOrderEvent event) {
        switch (event.eventType()) {
            case OrderEvents.OrderStarted -> {
                OrderEvents.OrderStartedEvent started = (OrderEvents.OrderStartedEvent) event.payload();
                this.status = OrderStatus.Created;
                this.paymentMethodId = started.paymentMethodId();
                this.amount = started.amount();
                this.currency = started.currency();
                this.lines = started.lines();
                if (started.sourceCommandId() != null) {
                    processedCommands.add(started.sourceCommandId());
                }
            }
            case OrderEvents.InventoryReserved -> {
                OrderEvents.InventoryReservedEvent reserved =
                    (OrderEvents.InventoryReservedEvent) event.payload();
                this.status = OrderStatus.InventoryReserved;
                this.reservationId = reserved.reservationId();
            }
            case OrderEvents.InventoryReservationFailed -> this.status = OrderStatus.Failed;
            case OrderEvents.PaymentAuthorized -> this.status = OrderStatus.PaymentAuthorized;
            case OrderEvents.PaymentFailed -> this.status = OrderStatus.Failed;
            case OrderEvents.OrderConfirmed -> this.status = OrderStatus.Confirmed;
            case OrderEvents.OrderFailed -> this.status = OrderStatus.Failed;
            default -> {
            }
        }
    }

    public boolean hasStarted() {
        return status != null;
    }

    public boolean isTerminal() {
        return OrderStatus.Confirmed.equals(status)
            || OrderStatus.Failed.equals(status);
    }

    public String status() {
        return status;
    }

    public List<OrderEvents.OrderLine> lines() {
        return lines;
    }

    public String paymentMethodId() {
        return paymentMethodId;
    }

    public double amount() {
        return amount;
    }

    public String currency() {
        return currency;
    }

    public boolean hasProcessedCommand(String sourceCommandId) {
        return processedCommands.contains(sourceCommandId);
    }

    public List<Object> start(StartOrderCommand command, String eventId, long now) {
        if (hasStarted()) {
            return List.of();
        }
        return List.of(new OrderEvents.OrderStartedEvent(
            eventId,
            command.idempotencyKey(),
            orderId,
            command.cartId(),
            command.shippingAddressId(),
            command.paymentMethodId(),
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
        if (isTerminal() || !OrderStatus.Created.equals(status)) {
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
        if (isTerminal() || !OrderStatus.InventoryReserved.equals(status)) {
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
        if (isTerminal() || !OrderStatus.PaymentAuthorized.equals(status)) {
            return List.of();
        }
        return List.of(new OrderEvents.OrderConfirmedEvent(eventId, orderId, now));
    }

    public record StoredOrderEvent(
        String eventId,
        String sourceCommandId,
        String orderId,
        String eventType,
        Object payload,
        long version,
        long createdAtUnixMs) {
    }

    public record InventoryReservationResult(boolean accepted, String reservationId, String reason) {
    }

    public record PaymentAuthorizationResult(boolean accepted, String paymentId, String reason) {
    }
}
