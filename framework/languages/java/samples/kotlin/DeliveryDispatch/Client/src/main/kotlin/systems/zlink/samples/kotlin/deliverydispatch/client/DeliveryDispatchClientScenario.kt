package systems.zlink.samples.kotlin.deliverydispatch.client

import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.future.await as futureAwait
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkKotlinStreamConnector
import systems.zlink.framework.kotlin.await
import systems.zlink.samples.kotlin.deliverydispatch.client.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.client.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CreateDeliveryRequest
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryCreated
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusNotify
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatuses
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDelivery
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryAccepted

class DeliveryDispatchClientScenario(
    private val channels: ZLinkClient,
) {
    suspend fun run(customer: ZLinkKotlinStreamConnector) {
        customer.connect().await()
        runSuccessfulDelivery(customer)
        runReassignedDelivery(customer)
        assertServerEvidence()
    }

    private suspend fun runSuccessfulDelivery(customer: ZLinkKotlinStreamConnector) = coroutineScope {
        val deliveryId = "delivery-success"
        val assigned = awaitStatus(customer, deliveryId, DeliveryStatuses.Assigned)
        val accepted = awaitStatus(customer, deliveryId, DeliveryStatuses.Accepted)
        val pickedUp = awaitStatus(customer, deliveryId, DeliveryStatuses.PickedUp)
        val delivered = awaitStatus(customer, deliveryId, DeliveryStatuses.Delivered)

        val subscribed = customer.request(SubscribeDelivery(deliveryId)).await<SubscribeDeliveryAccepted>()
        ensure(subscribed.deliveryId == deliveryId)

        val created = createDelivery(deliveryId)
        ensure(created.deliveryId == deliveryId)

        ensure(assigned.await().payload().courierId == SampleNames.CourierA)
        ensure(accepted.await().payload().courierId == SampleNames.CourierA)
        ensure(pickedUp.await().payload().courierId == SampleNames.CourierA)
        ensure(delivered.await().payload().courierId == SampleNames.CourierA)
    }

    private suspend fun runReassignedDelivery(customer: ZLinkKotlinStreamConnector) = coroutineScope {
        val deliveryId = "delivery-reassign"
        val assigned = awaitStatus(customer, deliveryId, DeliveryStatuses.Assigned)
        val reassigned = awaitStatus(customer, deliveryId, DeliveryStatuses.Reassigned)
        val accepted = awaitStatus(customer, deliveryId, DeliveryStatuses.Accepted)
        val delivered = awaitStatus(customer, deliveryId, DeliveryStatuses.Delivered)

        val subscribed = customer.request(SubscribeDelivery(deliveryId)).await<SubscribeDeliveryAccepted>()
        ensure(subscribed.deliveryId == deliveryId)

        val created = createDelivery(deliveryId)
        ensure(created.deliveryId == deliveryId)

        ensure(assigned.await().payload().courierId == SampleNames.CourierA)
        ensure(reassigned.await().payload().courierId == SampleNames.CourierB)
        ensure(accepted.await().payload().courierId == SampleNames.CourierB)
        ensure(delivered.await().payload().courierId == SampleNames.CourierB)
        println("deliverydispatch-reassignment=completed")
    }

    private fun kotlinx.coroutines.CoroutineScope.awaitStatus(
        customer: ZLinkKotlinStreamConnector,
        deliveryId: String,
        status: String,
    ): Deferred<systems.zlink.stream.connector.ZLinkStreamMessage<DeliveryStatusNotify>> {
        val wait = customer.waitFor<DeliveryStatusNotify>()
            .where { it.payload().deliveryId == deliveryId && it.payload().status == status }
            .timeout(SampleTimings.NotifyTimeout)
        return async { wait.await() }
    }

    private suspend fun createDelivery(deliveryId: String): DeliveryCreated =
        channels
            .requestToChannel(
                SampleNames.ApiChannel,
                CreateDeliveryRequest(deliveryId, "customer-1", "Kitchen 12", "Customer Lobby"),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(DeliveryCreated::class.java)
            .futureAwait()

    private suspend fun assertServerEvidence() {
        val assertion = channels
            .requestToChannel(
                SampleNames.ApiChannel,
                ServerAssertionReq("delivery-success", "delivery-reassign"),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(ServerAssertionRes::class.java)
            .futureAwait()
        ensure(assertion.passed)
        println("deliverydispatch-server-evidence=completed")
    }

    private fun ensure(condition: Boolean) {
        if (!condition) {
            throw IllegalStateException("Ensure failed")
        }
    }
}
