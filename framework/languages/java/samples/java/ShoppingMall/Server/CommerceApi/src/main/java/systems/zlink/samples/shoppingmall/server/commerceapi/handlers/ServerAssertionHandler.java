package systems.zlink.samples.shoppingmall.server.commerceapi.handlers;

import java.util.ArrayList;
import java.util.List;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore;
import systems.zlink.samples.shoppingmall.server.configuration.CommerceStore.StoreEvidence;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;

/**
 * Server-side assertion of the event-sourced evidence: per-order event
 * sequences, compensation counts, and idempotency started count.
 */
@ZLinkHandlerGroup("commerce")
public final class ServerAssertionHandler
    implements ZLinkRequestHandler<Messages.ServerAssertionReq, Messages.ServerAssertionRes> {
    private final CommerceStore store;

    public ServerAssertionHandler(CommerceStore store) {
        this.store = store;
    }

    @Override
    public Messages.ServerAssertionRes handle(
        Messages.ServerAssertionReq request,
        ZLinkRequestContext context) {
        List<String> orderIds = List.of(
            request.successfulOrderId(),
            request.pendingRecoveredOrderId(),
            request.inventoryFailureOrderId(),
            request.paymentFailureOrderId(),
            request.scaleOutOrderId());
        StoreEvidence evidence = store.evidence(orderIds);

        List<String> lines = new ArrayList<>();
        boolean passed = true;

        passed &= check(lines, evidence, request.successfulOrderId(), List.of(
            "OrderStartedEvent", "InventoryReservedEvent",
            "PaymentAuthorizedEvent", "OrderConfirmedEvent"));
        passed &= startsWith(lines, evidence, request.pendingRecoveredOrderId(), List.of(
            "OrderStartedEvent"));
        passed &= check(lines, evidence, request.inventoryFailureOrderId(), List.of(
            "OrderStartedEvent", "InventoryReservationFailedEvent", "OrderFailedEvent"));
        passed &= check(lines, evidence, request.paymentFailureOrderId(), List.of(
            "OrderStartedEvent", "InventoryReservedEvent", "PaymentFailedEvent",
            "InventoryReleasedEvent", "OrderFailedEvent"));
        passed &= check(lines, evidence, request.scaleOutOrderId(), List.of(
            "OrderStartedEvent", "InventoryReservedEvent",
            "PaymentAuthorizedEvent", "OrderConfirmedEvent"));

        boolean compensation = evidence.releasedReservationCount() >= 1
            && evidence.paymentFailureCount() >= 1;
        boolean startedCount = evidence.startedIdempotencyCount() == 5;
        lines.add("releasedReservationCount=" + evidence.releasedReservationCount());
        lines.add("paymentFailureCount=" + evidence.paymentFailureCount());
        lines.add("startedIdempotencyCount=" + evidence.startedIdempotencyCount());
        passed &= compensation && startedCount;

        System.err.printf("shoppingmall evidence: passed=%s%n", passed);
        return new Messages.ServerAssertionRes(passed, lines);
    }

    private boolean check(
        List<String> lines, StoreEvidence evidence, String orderId, List<String> expected) {
        List<String> actual = evidence.eventsByOrder().getOrDefault(orderId, List.of());
        boolean match = actual.equals(expected);
        lines.add(orderId + "=" + actual + (match ? " (ok)" : " (expected " + expected + ")"));
        return match;
    }

    private boolean startsWith(
        List<String> lines, StoreEvidence evidence, String orderId, List<String> prefix) {
        List<String> actual = evidence.eventsByOrder().getOrDefault(orderId, List.of());
        boolean match = actual.size() >= prefix.size()
            && actual.subList(0, prefix.size()).equals(prefix);
        lines.add(orderId + "=" + actual + (match ? " (ok prefix)" : " (expected prefix " + prefix + ")"));
        return match;
    }
}
