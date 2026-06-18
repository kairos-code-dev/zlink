package systems.zlink.samples.kotlin.shoppingmall.client

import java.util.concurrent.locks.LockSupport
import kotlinx.coroutines.future.await
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.samples.kotlin.shoppingmall.client.configuration.SampleNames
import systems.zlink.samples.kotlin.shoppingmall.client.configuration.SampleTimings
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.CreatePendingMappingReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.CreatePendingMappingRes
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.DeleteProjectionReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.DeleteProjectionRes
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.GetOrderStateReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.GetOrderStateRes
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.OrderState
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.OrderStatuses
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.RebuildProjectionApiReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.RebuildProjectionApiRes
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.ServerAssertionReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.ServerAssertionRes
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.StartOrderReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.StartOrderRes

/**
 * Reads as a checkout scenario test: each request is followed immediately by
 * its response assertion. The client only talks to the two CommerceApi
 * instances; storage and workflow internals are verified server-side.
 */
class ShoppingMallClientScenario(
    private val channels: ZLinkClient,
) {
    private val apiA = SampleNames.ApiInstanceA
    private val apiB = SampleNames.ApiInstanceB

    suspend fun run() {
        val success = runSuccessfulOrder()
        runDuplicateIdempotency(success)
        val pendingOrderId = runPendingRecovery()
        val inventoryOrderId = runInventoryFailure()
        val paymentOrderId = runPaymentFailure()
        runProjectionRebuild(success.orderId)
        runQueryConsistency(paymentOrderId)
        val scaleOrderId = runScaleOut()
        assertServerEvidence(success.orderId, pendingOrderId, inventoryOrderId, paymentOrderId, scaleOrderId)
    }

    private suspend fun runSuccessfulOrder(): StartOrderRes {
        val started = start(apiA, StartOrderReq("cart-success", "addr-home", "pm-ok", "order-success-001"))
        ensure(started.status == OrderStatuses.Created)
        ensure(started.orderId.isNotEmpty())

        val created = getState(apiA, started.orderId)
        ensure(created.isCreatedOrConfirmed())
        ensure(created.shippingAddressId == "addr-home")

        val confirmed = waitForStatus(apiA, started.orderId, OrderStatuses.Confirmed)
        ensure(confirmed.reservationId != null)
        ensure(confirmed.paymentId != null)
        ensure(confirmed.amount != null && Math.abs(confirmed.amount!! - 120.00) < 0.001)
        ensure(confirmed.currency == "USD")
        println("shoppingmall-success=completed")
        return started
    }

    private suspend fun runDuplicateIdempotency(success: StartOrderRes) {
        val duplicate = start(apiB, StartOrderReq("cart-success", "addr-home", "pm-ok", "order-success-001"))
        ensure(duplicate.orderId == success.orderId)
        println("shoppingmall-idempotency=completed")
    }

    private suspend fun runPendingRecovery(): String {
        val pending = channels
            .requestToChannel(
                SampleNames.commerceApiChannel(apiA),
                CreatePendingMappingReq("order-pending-001", "order-pending-0001", apiA),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(CreatePendingMappingRes::class.java)
            .await()
        ensure(pending.created)

        val started = start(apiB, StartOrderReq("cart-success", "addr-office", "pm-ok", "order-pending-001"))
        ensure(started.orderId == "order-pending-0001")
        ensure(started.status == OrderStatuses.Created)

        val created = getState(apiA, started.orderId)
        ensure(created.isCreatedOrConfirmed())
        ensure(created.shippingAddressId == "addr-office")
        println("shoppingmall-pending=completed")
        return started.orderId
    }

    private suspend fun runInventoryFailure(): String {
        val started = start(apiA, StartOrderReq("cart-inventory-fail", "addr-home", "pm-ok", "order-inventory-001"))
        val failed = waitForStatus(apiA, started.orderId, OrderStatuses.Failed)
        ensure(failed.reason != null && failed.reason!!.lowercase().contains("inventory"))
        val reread = getState(apiA, started.orderId)
        ensure(reread.status == OrderStatuses.Failed)
        println("shoppingmall-inventory-failure=completed")
        return started.orderId
    }

    private suspend fun runPaymentFailure(): String {
        val started = start(apiB, StartOrderReq("cart-payment-fail", "addr-home", "pm-decline", "order-payment-001"))
        val failed = waitForStatus(apiB, started.orderId, OrderStatuses.Failed)
        ensure(failed.reservationId != null)
        ensure(failed.reason != null && failed.reason!!.lowercase().contains("payment"))
        println("shoppingmall-payment-failure=completed")
        return started.orderId
    }

    private suspend fun runProjectionRebuild(orderId: String) {
        val deleted = channels
            .requestToChannel(SampleNames.commerceApiChannel(apiA), DeleteProjectionReq(orderId))
            .timeout(SampleTimings.RequestTimeout)
            .submit(DeleteProjectionRes::class.java)
            .await()
        ensure(deleted.deleted)

        val rebuilt = channels
            .requestToChannel(SampleNames.commerceApiChannel(apiA), RebuildProjectionApiReq(orderId))
            .timeout(SampleTimings.WorkflowTimeout)
            .submit(RebuildProjectionApiRes::class.java)
            .await()
        ensure(rebuilt.state.status == OrderStatuses.Confirmed)

        val fromB = getState(apiB, orderId)
        ensure(fromB.status == OrderStatuses.Confirmed)
        println("shoppingmall-rebuild=completed")
    }

    private suspend fun runQueryConsistency(orderId: String) {
        val fromB = getState(apiB, orderId)
        val fromA = getState(apiA, orderId)
        ensure(fromA.status == fromB.status)
        ensure(fromA.status == OrderStatuses.Failed)
        println("shoppingmall-consistency=completed")
    }

    private suspend fun runScaleOut(): String {
        val started = start(apiB, StartOrderReq("cart-success", "addr-office", "pm-ok", "order-scale-001"))
        val confirmed = waitForStatus(apiA, started.orderId, OrderStatuses.Confirmed)
        ensure(confirmed.status == OrderStatuses.Confirmed)
        println("shoppingmall-scaleout=completed")
        return started.orderId
    }

    private suspend fun assertServerEvidence(
        successOrderId: String,
        pendingOrderId: String,
        inventoryOrderId: String,
        paymentOrderId: String,
        scaleOrderId: String,
    ) {
        val assertion = channels
            .requestToChannel(
                SampleNames.commerceApiChannel(apiA),
                ServerAssertionReq(successOrderId, pendingOrderId, inventoryOrderId, paymentOrderId, scaleOrderId),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(ServerAssertionRes::class.java)
            .await()
        ensure(assertion.passed)
        println("shoppingmall-server-evidence=completed")
    }

    private suspend fun start(instanceId: String, request: StartOrderReq): StartOrderRes =
        channels
            .requestToChannel(SampleNames.commerceApiChannel(instanceId), request)
            .timeout(SampleTimings.RequestTimeout)
            .submit(StartOrderRes::class.java)
            .await()

    private suspend fun getState(instanceId: String, orderId: String): OrderState =
        channels
            .requestToChannel(SampleNames.commerceApiChannel(instanceId), GetOrderStateReq(orderId))
            .timeout(SampleTimings.RequestTimeout)
            .submit(GetOrderStateRes::class.java)
            .await()
            .state

    private suspend fun waitForStatus(instanceId: String, orderId: String, expectedStatus: String): OrderState {
        val deadline = System.nanoTime() + SampleTimings.WorkflowTimeout.toNanos()
        var last: OrderState? = null
        while (System.nanoTime() < deadline) {
            last = getState(instanceId, orderId)
            if (last.status == expectedStatus) {
                return last
            }
            LockSupport.parkNanos(SampleTimings.PollDelay.toNanos())
        }
        throw IllegalStateException(
            "Order '$orderId' did not reach status '$expectedStatus' (last=${last?.status ?: "none"}).",
        )
    }

    private fun ensure(condition: Boolean) {
        if (!condition) {
            throw IllegalStateException("Ensure failed")
        }
    }

    private fun OrderState.isCreatedOrConfirmed(): Boolean =
        status == OrderStatuses.Created || status == OrderStatuses.Confirmed
}
