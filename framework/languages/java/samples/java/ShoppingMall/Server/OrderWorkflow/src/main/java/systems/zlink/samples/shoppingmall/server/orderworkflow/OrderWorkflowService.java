package systems.zlink.samples.shoppingmall.server.orderworkflow;

import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.shoppingmall.server.orderworkflow.spots.OrderWorkflowSpot;
import systems.zlink.samples.shoppingmall.server.configuration.SampleNames;
import systems.zlink.samples.shoppingmall.server.configuration.SampleTimings;
import systems.zlink.samples.shoppingmall.server.configuration.SampleTopology;
import systems.zlink.samples.shoppingmall.server.shared.domain.OrderDomain;
import systems.zlink.samples.shoppingmall.server.shared.domain.OrderProjection;
import systems.zlink.samples.shoppingmall.server.shared.store.RedisCommerceStore;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;

public final class OrderWorkflowService {
    private final RedisCommerceStore store;
    private final ZLinkSpotManager spots;
    private final ZLinkRouteClient routes;
    private final SpotHandleResolver spotHandles;

    public OrderWorkflowService(
        RedisCommerceStore store,
        ZLinkSpotManager spots,
        ZLinkRouteClient routes,
        SpotHandleResolver spotHandles) {
        this.store = store;
        this.spots = spots;
        this.routes = routes;
        this.spotHandles = spotHandles;
    }

    public CompletionStage<Messages.OrderState> start(Messages.StartOrderWorkflowReq request) {
        return orderSpot(request.orderId()).thenCompose(address -> routes
            .requestToSpot(SampleNames.OrderSpotDiscovery, address, request)
            .timeout(SampleTimings.WorkflowTimeout)
            .submit(Messages.StartOrderWorkflowRes.class))
            .thenApply(Messages.StartOrderWorkflowRes::state);
    }

    public CompletionStage<Messages.OrderState> continueOrder(String orderId) {
        return orderSpot(orderId).thenCompose(address -> routes.requestToSpot(
                SampleNames.OrderSpotDiscovery,
                address,
                new Messages.ContinueOrderWorkflowReq(orderId))
            .timeout(SampleTimings.WorkflowTimeout)
            .submit(Messages.ContinueOrderWorkflowRes.class))
            .thenApply(Messages.ContinueOrderWorkflowRes::state);
    }

    public CompletionStage<Messages.OrderState> prepareInventoryReserved(Messages.StartOrderWorkflowReq request) {
        return orderSpot(request.orderId()).thenCompose(address -> routes.requestToSpot(
                SampleNames.OrderSpotDiscovery,
                address,
                new Messages.PrepareInventoryReservedCheckpointReq(request))
            .timeout(SampleTimings.WorkflowTimeout)
            .submit(Messages.ContinueOrderWorkflowRes.class))
            .thenApply(Messages.ContinueOrderWorkflowRes::state);
    }

    public CompletionStage<Messages.OrderState> rebuildProjection(String orderId) {
        return orderSpot(orderId).thenCompose(address -> routes.requestToSpot(
                SampleNames.OrderSpotDiscovery,
                address,
                new Messages.RebuildOrderProjectionReq(orderId))
            .timeout(SampleTimings.WorkflowTimeout)
            .submit(Messages.RebuildOrderProjectionRes.class))
            .thenApply(Messages.RebuildOrderProjectionRes::state);
    }

    public CompletionStage<Messages.OrderState> startInSpot(
        OrderWorkflowSpot spot,
        Messages.StartOrderWorkflowReq request) {
        List<OrderDomain.StoredOrderEvent> existing = store.readEvents(request.orderId());
        Messages.OrderState current = store.findProjection(request.orderId());
        if (current != null) {
            return java.util.concurrent.CompletableFuture.completedFuture(current);
        }
        if (existing.isEmpty()) {
            append(
                request.orderId(),
                0,
                List.of(new OrderDomain.OrderStartedEvent(
                    RedisCommerceStore.eventId("order-started", request.orderId()),
                    request.idempotencyKey(),
                    request.orderId(),
                    request.cartId(),
                    request.shippingAddressId(),
                    request.lines(),
                    request.amount(),
                    request.currency(),
                    RedisCommerceStore.nowMs())));
        } else {
            saveProjection(request.orderId());
        }
        return spotHandles.resolveSpotHandle(spot.context().spotRid())
            .thenApply(found -> {
                SpotHandle handle = found.orElseThrow(() -> new IllegalStateException(
                    "order Spot is not registered: " + spot.context().spotRid()));
                spot.context().outbound().sendToSpot(
                    handle,
                    new Messages.RunOrderWorkflowCommand(request.orderId())).submit();
                return store.findProjection(request.orderId());
            });
    }

    public Messages.OrderState continueOrderInSpot(String orderId) {
        Messages.OrderState state = saveProjection(orderId);
        if (Messages.OrderStatuses.Created.equals(state.status())) {
            state = reserveInventory(orderId, state);
        }
        if (Messages.OrderStatuses.InventoryReserved.equals(state.status())) {
            state = authorizePayment(orderId, state);
        }
        if (Messages.OrderStatuses.PaymentAuthorized.equals(state.status())) {
            state = confirm(orderId);
        }
        return state;
    }

    public Messages.OrderState prepareInventoryReservedInSpot(Messages.StartOrderWorkflowReq request) {
        List<OrderDomain.StoredOrderEvent> existing = store.readEvents(request.orderId());
        if (existing.isEmpty()) {
            append(
                request.orderId(),
                0,
                List.of(
                    new OrderDomain.OrderStartedEvent(
                        RedisCommerceStore.eventId("order-started", request.orderId()),
                        request.idempotencyKey(),
                        request.orderId(),
                        request.cartId(),
                        request.shippingAddressId(),
                        request.lines(),
                        request.amount(),
                        request.currency(),
                        RedisCommerceStore.nowMs()),
                    new OrderDomain.InventoryReservedEvent(
                        RedisCommerceStore.eventId("inventory-reserved", request.orderId()),
                        request.orderId(),
                        "reservation-" + request.orderId(),
                        RedisCommerceStore.nowMs())));
        }
        return saveProjection(request.orderId());
    }

    public Messages.OrderState rebuildProjectionInSpot(String orderId) {
        return store.rebuildProjection(orderId);
    }

    private Messages.OrderState reserveInventory(
        String orderId,
        Messages.OrderState state) {
        List<OrderDomain.StoredOrderEvent> events = store.readEvents(orderId);
        OrderDomain.OrderStartedEvent started = started(events);
        OrderDomain.ReserveInventoryResult reserved = store.reserveInventory(
            orderId,
            "reservation-" + orderId,
            started.lines());
        if (!reserved.accepted()) {
            append(
                orderId,
                version(events),
                List.of(
                    new OrderDomain.InventoryReservationFailedEvent(
                        RedisCommerceStore.eventId("inventory-failed", orderId),
                        orderId,
                        reserved.reason(),
                        RedisCommerceStore.nowMs()),
                    new OrderDomain.OrderFailedEvent(
                        RedisCommerceStore.eventId("order-failed", orderId),
                        orderId,
                        reserved.reason(),
                        RedisCommerceStore.nowMs())));
            return store.findProjection(orderId);
        }
        append(
            orderId,
            version(events),
            List.of(new OrderDomain.InventoryReservedEvent(
                RedisCommerceStore.eventId("inventory-reserved", orderId),
                orderId,
                reserved.reservationId(),
                RedisCommerceStore.nowMs())));
        return store.findProjection(orderId);
    }

    private Messages.OrderState authorizePayment(
        String orderId,
        Messages.OrderState state) {
        List<OrderDomain.StoredOrderEvent> events = store.readEvents(orderId);
        String paymentMethodId = store.getOrderPaymentMethod(orderId);
        OrderDomain.AuthorizePaymentResult paid = store.authorizePayment(
            orderId,
            "payment-" + orderId,
            paymentMethodId,
            state.amount(),
            state.currency());
        if (!paid.accepted()) {
            append(
                orderId,
                version(events),
                List.of(
                    new OrderDomain.PaymentFailedEvent(
                        RedisCommerceStore.eventId("payment-failed", orderId),
                        orderId,
                        paid.reason(),
                        RedisCommerceStore.nowMs()),
                    new OrderDomain.InventoryReleasedEvent(
                        RedisCommerceStore.eventId("inventory-released", orderId),
                        orderId,
                        state.reservationId(),
                        paid.reason(),
                        RedisCommerceStore.nowMs()),
                    new OrderDomain.OrderFailedEvent(
                        RedisCommerceStore.eventId("order-failed", orderId),
                        orderId,
                        paid.reason(),
                        RedisCommerceStore.nowMs())));
            store.releaseInventory(orderId, state.reservationId(), paid.reason());
            return store.findProjection(orderId);
        }
        append(
            orderId,
            version(events),
            List.of(new OrderDomain.PaymentAuthorizedEvent(
                RedisCommerceStore.eventId("payment-authorized", orderId),
                orderId,
                paid.paymentId(),
                RedisCommerceStore.nowMs())));
        return store.findProjection(orderId);
    }

    private Messages.OrderState confirm(String orderId) {
        List<OrderDomain.StoredOrderEvent> events = store.readEvents(orderId);
        append(
            orderId,
            version(events),
            List.of(new OrderDomain.OrderConfirmedEvent(
                RedisCommerceStore.eventId("order-confirmed", orderId),
                orderId,
                RedisCommerceStore.nowMs())));
        return store.findProjection(orderId);
    }

    private void append(
        String orderId,
        long expectedVersion,
        List<OrderDomain.OrderEvent> events) {
        store.appendEvents(orderId, expectedVersion, events);
        saveProjection(orderId);
    }

    private Messages.OrderState saveProjection(String orderId) {
        Messages.OrderState state = OrderProjection.fold(store.readEvents(orderId));
        if (state == null) {
            throw new IllegalStateException("Order '" + orderId + "' has no events.");
        }
        store.saveProjection(state);
        return state;
    }

    private CompletionStage<SpotHandle> orderSpot(String orderId) {
        RoutingId spotRid = RoutingId.from(orderId);
        return spots.getOrCreate(OrderWorkflowSpot.class, spotRid)
            .thenCompose(ignored -> spotHandles.resolveSpotHandle(spotRid))
            .thenApply(found -> found.orElseThrow(() ->
                new IllegalStateException("order Spot is not registered: " + spotRid)));
    }

    private static OrderDomain.OrderStartedEvent started(List<OrderDomain.StoredOrderEvent> events) {
        for (OrderDomain.StoredOrderEvent event : events) {
            if (event.event() instanceof OrderDomain.OrderStartedEvent started) {
                return started;
            }
        }
        throw new IllegalStateException("Order stream has no OrderStartedEvent.");
    }

    private static long version(List<OrderDomain.StoredOrderEvent> events) {
        return events.isEmpty() ? 0 : events.getLast().version();
    }
}
