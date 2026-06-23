package systems.zlink.samples.shoppingmall.server.orderworkflow;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import org.springframework.stereotype.Component;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore.AuthorizePaymentResult;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore.ReserveInventoryResult;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore.StoredEvent;
import systems.zlink.samples.shoppingmall.server.orderworkflow.domain.OrderAggregate;
import systems.zlink.samples.shoppingmall.server.orderworkflow.domain.OrderAggregate.InventoryReservationResult;
import systems.zlink.samples.shoppingmall.server.orderworkflow.domain.OrderAggregate.PaymentAuthorizationResult;
import systems.zlink.samples.shoppingmall.server.orderworkflow.domain.OrderAggregate.StoredOrderEvent;
import systems.zlink.samples.shoppingmall.server.orderworkflow.domain.OrderEvents;
import systems.zlink.samples.shoppingmall.server.orderworkflow.domain.OrderProjection;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.OrderState;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.StartOrderWorkflowReq;

/**
 * Application service that owns the event-sourced order workflow: it loads the
 * event stream, rebuilds the aggregate, appends new events with optimistic
 * version checks, advances the saga, and refreshes the projection.
 */
@Component
public final class OrderWorkflowService {
    private static final int MAX_WORKFLOW_STEPS = 8;

    private final CommerceStore store;
    private final ObjectMapper json;
    private final OrderAggregate.OrderEventCodec codec;

    public OrderWorkflowService(CommerceStore store) {
        this.store = store;
        this.json = store.json();
        this.codec = new JacksonCodec(json);
    }

    public OrderState start(StartOrderWorkflowReq command) {
        List<StoredEvent> stored = store.readEvents(command.orderId());
        OrderAggregate aggregate = OrderAggregate.rehydrate(command.orderId(), toDomainEvents(stored), codec);
        if (aggregate.hasProcessedCommand(command.idempotencyKey())) {
            return requireProjection(command.orderId());
        }
        List<Object> events = aggregate.start(command, newEventId("started", command.orderId()), now());
        if (!events.isEmpty()) {
            appendAndProject(command.orderId(), stored.size(), events);
        }
        store.markIdempotencyStarted(command.idempotencyKey());
        return requireProjection(command.orderId());
    }

    public OrderState continueWorkflow(String orderId) {
        String paymentMethodId = store.orderPaymentMethod(orderId);
        // Each iteration advances one event-stream transition; a started order
        // reaches a terminal state within a handful of steps.
        for (int step = 0; step < MAX_WORKFLOW_STEPS; step++) {
            List<StoredEvent> stored = store.readEvents(orderId);
            OrderAggregate aggregate = OrderAggregate.rehydrate(orderId, toDomainEvents(stored), codec);
            if (aggregate.isTerminal() || !aggregate.hasStarted()) {
                return projection(orderId).orElseGet(() -> store.placeholder(orderId));
            }
            String status = aggregate.status();
            List<Object> next;
            long ts = now();
            if (Messages.OrderStatuses.Created.equals(status)) {
                ReserveInventoryResult reserve = store.reserveInventory(orderId, aggregate.lines());
                next = aggregate.applyInventoryResult(
                    new InventoryReservationResult(
                        reserve.accepted(),
                        reserve.reservationId(),
                        reserve.reason()),
                    newEventId("reserved", orderId),
                    newEventId("inv-fail", orderId),
                    ts);
            } else if (Messages.OrderStatuses.InventoryReserved.equals(status)) {
                AuthorizePaymentResult payment = store.authorizePayment(
                    orderId,
                    paymentMethodId,
                    amountFromStarted(stored),
                    currencyFromStarted(stored));
                next = aggregate.applyPaymentResult(
                    new PaymentAuthorizationResult(
                        payment.accepted(),
                        payment.paymentId(),
                        payment.reason()),
                    newEventId("paid", orderId),
                    newEventId("released", orderId),
                    newEventId("pay-fail", orderId),
                    ts);
            } else if (Messages.OrderStatuses.PaymentAuthorized.equals(status)) {
                next = aggregate.confirm(newEventId("confirmed", orderId), ts);
            } else {
                next = List.of();
            }
            if (next.isEmpty()) {
                return requireProjection(orderId);
            }
            appendAndProject(orderId, stored.size(), next);
            for (Object event : next) {
                if (event instanceof OrderEvents.InventoryReleasedEvent released) {
                    store.releaseInventory(orderId, released.reservationId(), released.reason());
                }
            }
        }
        return projection(orderId).orElseGet(() -> store.placeholder(orderId));
    }

    public OrderState rebuildProjection(String orderId) {
        List<StoredEvent> stored = store.readEvents(orderId);
        if (stored.isEmpty()) {
            throw new IllegalStateException("No order event stream for '" + orderId + "'.");
        }
        OrderState state = null;
        for (StoredEvent event : stored) {
            state = OrderProjection.apply(state, event.eventType(), event.payload());
        }
        store.saveReadModel(state);
        return state;
    }

    private void appendAndProject(String orderId, long expectedVersion, List<Object> events) {
        List<StoredEvent> toStore = new ArrayList<>();
        for (Object event : events) {
            toStore.add(toStored(orderId, event));
        }
        store.appendEvents(orderId, expectedVersion, toStore);
        OrderState state = store.findReadModel(orderId).orElse(null);
        for (StoredEvent stored : store.readEvents(orderId)) {
            // fold the full stream so projection stays consistent with dedupe
            state = OrderProjection.apply(state == null ? null : state, stored.eventType(), stored.payload());
        }
        store.saveReadModel(state);
    }

    private StoredEvent toStored(String orderId, Object event) {
        JsonNode payload = json.valueToTree(event);
        String eventType = event.getClass().getSimpleName();
        String eventId = payload.path("eventId").asText(newEventId("evt", orderId));
        String sourceCommandId = null;
        if (event instanceof OrderEvents.OrderStartedEvent started) {
            sourceCommandId = started.sourceCommandId();
        }
        long createdAt = payload.path("createdAtUnixMs").asLong(
            payload.path("confirmedAtUnixMs").asLong(
                payload.path("failedAtUnixMs").asLong(now())));
        return new StoredEvent(eventId, sourceCommandId, orderId, eventType, payload, 0, createdAt);
    }

    private List<StoredOrderEvent> toDomainEvents(List<StoredEvent> stored) {
        return stored.stream()
            .map(event -> new StoredOrderEvent(
                event.eventId(),
                event.sourceCommandId(),
                event.orderId(),
                event.eventType(),
                event.payload(),
                event.version(),
                event.createdAtUnixMs()))
            .toList();
    }

    private OrderState requireProjection(String orderId) {
        return projection(orderId).orElseThrow(() ->
            new IllegalStateException("Projection missing for '" + orderId + "'."));
    }

    private Optional<OrderState> projection(String orderId) {
        return store.findReadModel(orderId);
    }

    private double amountFromStarted(List<StoredEvent> stored) {
        for (StoredEvent event : stored) {
            if (OrderEvents.OrderStarted.equals(event.eventType())) {
                return event.payload().path("amount").asDouble();
            }
        }
        return 0.0;
    }

    private String currencyFromStarted(List<StoredEvent> stored) {
        for (StoredEvent event : stored) {
            if (OrderEvents.OrderStarted.equals(event.eventType())) {
                return event.payload().path("currency").asText("USD");
            }
        }
        return "USD";
    }

    private static String newEventId(String prefix, String orderId) {
        return prefix + "-" + orderId + "-" + UUID.randomUUID().toString().replace("-", "");
    }

    private static long now() {
        return System.currentTimeMillis();
    }

    private record JacksonCodec(ObjectMapper json) implements OrderAggregate.OrderEventCodec {
        @Override
        public <T> T decode(JsonNode payload, Class<T> type) {
            return json.convertValue(payload, type);
        }
    }
}
