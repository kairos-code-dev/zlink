package systems.zlink.samples.shoppingmall.client;

import java.util.concurrent.locks.LockSupport;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.shoppingmall.client.configuration.SampleNames;
import systems.zlink.samples.shoppingmall.client.configuration.SampleTimings;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.OrderState;
import systems.zlink.samples.shoppingmall.shared.contracts.Messages.OrderStatuses;

/**
 * Reads as a checkout scenario test: each request is followed immediately by
 * its response assertion. The client only ever talks to the two CommerceApi
 * instances; storage and workflow internals are verified server-side.
 */
public final class ShoppingMallClientScenario {
    private static final String ApiA = SampleNames.ApiInstanceA;
    private static final String ApiB = SampleNames.ApiInstanceB;

    private final ZLinkClient channels;

    public ShoppingMallClientScenario(ZLinkClient channels) {
        this.channels = channels;
    }

    public void run() {
        Messages.StartOrderRes success = runSuccessfulOrder();
        runDuplicateIdempotency(success);
        String pendingOrderId = runPendingRecovery();
        String inventoryOrderId = runInventoryFailure();
        String paymentOrderId = runPaymentFailure();
        runProjectionRebuild(success.orderId());
        runQueryConsistency(paymentOrderId);
        String scaleOrderId = runScaleOut();
        assertServerEvidence(
            success.orderId(), pendingOrderId, inventoryOrderId, paymentOrderId, scaleOrderId);
    }

    private Messages.StartOrderRes runSuccessfulOrder() {
        Messages.StartOrderRes started = start(ApiA, new Messages.StartOrderReq(
            "cart-success", "addr-home", "pm-ok", "order-success-001"));
        ensure(OrderStatuses.Created.equals(started.status()));
        ensure(!started.orderId().isEmpty());

        OrderState created = getState(ApiA, started.orderId());
        ensure(isCreatedOrConfirmed(created));
        ensure("addr-home".equals(created.shippingAddressId()));

        OrderState confirmed = waitForStatus(ApiA, started.orderId(), OrderStatuses.Confirmed);
        ensure(confirmed.reservationId() != null);
        ensure(confirmed.paymentId() != null);
        ensure(confirmed.amount() != null && Math.abs(confirmed.amount() - 120.00) < 0.001);
        ensure("USD".equals(confirmed.currency()));
        System.out.println("shoppingmall-success=completed");
        return started;
    }

    private void runDuplicateIdempotency(Messages.StartOrderRes success) {
        Messages.StartOrderRes duplicate = start(ApiB, new Messages.StartOrderReq(
            "cart-success", "addr-home", "pm-ok", "order-success-001"));
        ensure(duplicate.orderId().equals(success.orderId()));
        System.out.println("shoppingmall-idempotency=completed");
    }

    private String runPendingRecovery() {
        Messages.CreatePendingMappingRes pending = channels
            .requestToChannel(
                SampleNames.commerceApiChannel(ApiA),
                new Messages.CreatePendingMappingReq("order-pending-001", "order-pending-0001", ApiA))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.CreatePendingMappingRes.class);
        ensure(pending.created());

        Messages.StartOrderRes started = start(ApiB, new Messages.StartOrderReq(
            "cart-success", "addr-office", "pm-ok", "order-pending-001"));
        ensure("order-pending-0001".equals(started.orderId()));
        ensure(OrderStatuses.Created.equals(started.status()));

        OrderState created = waitForCreatedOrConfirmed(ApiA, started.orderId());
        ensure(isCreatedOrConfirmed(created));
        ensure("addr-office".equals(created.shippingAddressId()));
        System.out.println("shoppingmall-pending=completed");
        return started.orderId();
    }

    private String runInventoryFailure() {
        Messages.StartOrderRes started = start(ApiA, new Messages.StartOrderReq(
            "cart-inventory-fail", "addr-home", "pm-ok", "order-inventory-001"));
        OrderState failed = waitForStatus(ApiA, started.orderId(), OrderStatuses.Failed);
        ensure(failed.reason() != null && failed.reason().toLowerCase().contains("inventory"));
        OrderState reread = getState(ApiA, started.orderId());
        ensure(OrderStatuses.Failed.equals(reread.status()));
        System.out.println("shoppingmall-inventory-failure=completed");
        return started.orderId();
    }

    private String runPaymentFailure() {
        Messages.StartOrderRes started = start(ApiB, new Messages.StartOrderReq(
            "cart-payment-fail", "addr-home", "pm-decline", "order-payment-001"));
        OrderState failed = waitForStatus(ApiB, started.orderId(), OrderStatuses.Failed);
        ensure(failed.reservationId() != null);
        ensure(failed.reason() != null && failed.reason().toLowerCase().contains("payment"));
        System.out.println("shoppingmall-payment-failure=completed");
        return started.orderId();
    }

    private void runProjectionRebuild(String orderId) {
        Messages.DeleteProjectionRes deleted = channels
            .requestToChannel(
                SampleNames.commerceApiChannel(ApiA),
                new Messages.DeleteProjectionReq(orderId))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.DeleteProjectionRes.class);
        ensure(deleted.deleted());

        Messages.RebuildProjectionApiRes rebuilt = channels
            .requestToChannel(
                SampleNames.commerceApiChannel(ApiA),
                new Messages.RebuildProjectionApiReq(orderId))
            .timeout(SampleTimings.WorkflowTimeout)
            .await(Messages.RebuildProjectionApiRes.class);
        ensure(OrderStatuses.Confirmed.equals(rebuilt.state().status()));

        OrderState fromB = getState(ApiB, orderId);
        ensure(OrderStatuses.Confirmed.equals(fromB.status()));
        System.out.println("shoppingmall-rebuild=completed");
    }

    private void runQueryConsistency(String orderId) {
        OrderState fromB = getState(ApiB, orderId);
        OrderState fromA = getState(ApiA, orderId);
        ensure(fromA.status().equals(fromB.status()));
        ensure(OrderStatuses.Failed.equals(fromA.status()));
        System.out.println("shoppingmall-consistency=completed");
    }

    private String runScaleOut() {
        Messages.StartOrderRes started = start(ApiB, new Messages.StartOrderReq(
            "cart-success", "addr-office", "pm-ok", "order-scale-001"));
        OrderState confirmed = waitForStatus(ApiA, started.orderId(), OrderStatuses.Confirmed);
        ensure(OrderStatuses.Confirmed.equals(confirmed.status()));
        System.out.println("shoppingmall-scaleout=completed");
        return started.orderId();
    }

    private void assertServerEvidence(
        String successOrderId,
        String pendingOrderId,
        String inventoryOrderId,
        String paymentOrderId,
        String scaleOrderId) {
        Messages.ServerAssertionRes assertion = channels
            .requestToChannel(
                SampleNames.commerceApiChannel(ApiA),
                new Messages.ServerAssertionReq(
                    successOrderId, pendingOrderId, inventoryOrderId, paymentOrderId, scaleOrderId))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.ServerAssertionRes.class);
        ensure(assertion.passed());
        System.out.println("shoppingmall-server-evidence=completed");
    }

    private Messages.StartOrderRes start(String instanceId, Messages.StartOrderReq request) {
        return channels
            .requestToChannel(SampleNames.commerceApiChannel(instanceId), request)
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.StartOrderRes.class);
    }

    private OrderState getState(String instanceId, String orderId) {
        Messages.GetOrderStateRes response = channels
            .requestToChannel(
                SampleNames.commerceApiChannel(instanceId),
                new Messages.GetOrderStateReq(orderId))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.GetOrderStateRes.class);
        return response.state();
    }

    private OrderState waitForStatus(String instanceId, String orderId, String expectedStatus) {
        long deadline = System.nanoTime() + SampleTimings.WorkflowTimeout.toNanos();
        OrderState last = null;
        while (System.nanoTime() < deadline) {
            last = getState(instanceId, orderId);
            if (expectedStatus.equals(last.status())) {
                return last;
            }
            LockSupport.parkNanos(SampleTimings.PollDelay.toNanos());
        }
        throw new IllegalStateException(
            "Order '" + orderId + "' did not reach status '" + expectedStatus
                + "' (last=" + (last == null ? "none" : last.status()) + ").");
    }

    private OrderState waitForCreatedOrConfirmed(String instanceId, String orderId) {
        long deadline = System.nanoTime() + SampleTimings.WorkflowTimeout.toNanos();
        OrderState last = null;
        while (System.nanoTime() < deadline) {
            last = getState(instanceId, orderId);
            if (isCreatedOrConfirmed(last)) {
                return last;
            }
            LockSupport.parkNanos(SampleTimings.PollDelay.toNanos());
        }
        throw new IllegalStateException(
            "Order '" + orderId + "' did not reach Created or Confirmed"
                + " (last=" + (last == null ? "none" : last.status()) + ").");
    }

    private static boolean isCreatedOrConfirmed(OrderState state) {
        return OrderStatuses.Created.equals(state.status())
            || OrderStatuses.Confirmed.equals(state.status());
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("Ensure failed");
        }
    }
}
