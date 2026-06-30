package systems.zlink.samples.kotlin.deliverydispatch.client

import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import systems.zlink.framework.kotlin.ZLinkKotlinStreamConnector
import systems.zlink.framework.kotlin.await
import systems.zlink.httpclient.ZLinkHttpClient
import systems.zlink.httpclient.kotlin.fetch
import systems.zlink.samples.kotlin.deliverydispatch.client.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CreateDeliveryRequest
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryCreated
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusNotify
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatuses
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDelivery
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryAccepted

class DeliveryDispatchClientScenario(
    private val api: ZLinkHttpClient,
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
        return async { wait.await() }
    }

    private suspend fun createDelivery(deliveryId: String): DeliveryCreated =
        api.post("/deliveries")
            .body(CreateDeliveryRequest(deliveryId, "customer-1", "Kitchen 12", "Customer Lobby"))
            .fetch()

    private suspend fun assertServerEvidence() {
        val assertion = api.post("/self-check/assert")
            .body(ServerAssertionReq("delivery-success", "delivery-reassign"))
            .fetch<ServerAssertionRes>()
        ensure(assertion.passed)
        println("deliverydispatch-server-evidence=completed")
    }

    private fun ensure(condition: Boolean) {
        if (!condition) {
            throw IllegalStateException("Ensure failed")
        }
    }
}
