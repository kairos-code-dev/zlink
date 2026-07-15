package systems.zlink.samples.kotlin.deliverydispatch.client

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.KotlinModule
import java.time.Duration
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.util.Collections
import java.util.concurrent.CompletionStage
import kotlinx.coroutines.runBlocking
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.awaitReply
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTopology
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierSessionReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.BindCourierSessionRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CourierDecisionMsg
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CreateDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CreateDeliveryRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatus
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusNotify
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryNotify
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeDeliveryRes
import systems.zlink.stream.connector.ZLinkStreamConnector
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions
import systems.zlink.stream.connector.ZLinkStreamDispatchMode
import systems.zlink.stream.connector.ZLinkStreamMessage

fun main() {
    runBlocking {
        DeliveryDispatchClientScenario().run()
    }
}

class DeliveryDispatchClientScenario {
    private val json = ObjectMapper().registerModule(KotlinModule.Builder().build())
    private val http = HttpClient.newHttpClient()

    suspend fun run() {
        println(SampleNames.TopologyReadyMarker)
        runStreamRuntime()
    }

    private suspend fun runStreamRuntime() {
        val customer = createClient(SampleTopology.CustomerStreamEndpoint)
        val courierA = createClient(SampleTopology.CourierStreamEndpoint)
        val courierB = createClient(SampleTopology.CourierStreamEndpoint)
        try {
            customer.connect().submit().await()
            courierA.connect().submit().await()
            courierB.connect().submit().await()

            val courierABound = courierA
                .request(BindCourierSessionReq("courier-a"))
                .submit(BindCourierSessionRes::class.java).await()
            check(courierABound.courierId == "courier-a")
            println("deliverydispatch-bind=courier-a")
            val courierBBound = courierB
                .request(BindCourierSessionReq("courier-b"))
                .submit(BindCourierSessionRes::class.java).await()
            check(courierBBound.courierId == "courier-b")
            check(courierABound.actor.nodeRid != courierBBound.actor.nodeRid)
            println("deliverydispatch-bind=courier-b")

            runSuccessfulDelivery(customer, courierA)
            runReassignedDelivery(customer, courierA, courierB)
            assertServerEvidence()
            println(SampleNames.CompletedMarker)
        } finally {
            customer.close().submit().await()
            courierA.close().submit().await()
            courierB.close().submit().await()
        }
    }

    private suspend fun runSuccessfulDelivery(
        customer: ZLinkStreamConnector,
        courier: ZLinkStreamConnector,
    ) {
        val deliveryId = "delivery-success"
        val offer = courier
            .waitFor(OfferDeliveryNotify::class.java)
            .where(OfferDeliveryNotify::class.java) { message -> message.payload().deliveryId == deliveryId }
            .submit(OfferDeliveryNotify::class.java)
        val expected = listOf(
            DeliveryStatus.Assigned,
            DeliveryStatus.Accepted,
            DeliveryStatus.PickedUp,
            DeliveryStatus.Delivered,
        )
        val statuses = waitStatuses(customer, deliveryId, expected)

        val subscribed = customer
            .request(SubscribeDeliveryReq(deliveryId))
            .submit(SubscribeDeliveryRes::class.java).await()
        check(subscribed.deliveryId == deliveryId)
        println("deliverydispatch-subscribe=$deliveryId")

        val created = post(
            path = "/deliveries",
            body = CreateDeliveryReq(
                deliveryId = deliveryId,
                customerId = "customer-1",
                pickupAddress = "Kitchen 12",
                dropoffAddress = "Customer Lobby",
            ),
            responseType = CreateDeliveryRes::class.java,
        )
        check(created.deliveryId == deliveryId)
        println("deliverydispatch-create=$deliveryId")

        val courierOffer = offer.await().payload()
        println("deliverydispatch-offer=$deliveryId:${courierOffer.courierId}")
        courier
            .send(CourierDecisionMsg(courierOffer.deliveryId, courierOffer.courierId, true, null))
            .submit()

        val notifications = statuses.waits.map { it.await().payload() }
        check(statuses.arrivals.toList() == expected)
        check(notifications.all { it.courierId == "courier-a" })
    }

    private suspend fun runReassignedDelivery(
        customer: ZLinkStreamConnector,
        courierA: ZLinkStreamConnector,
        courierB: ZLinkStreamConnector,
    ) {
        val deliveryId = "delivery-reassign"
        val firstOffer = courierA
            .waitFor(OfferDeliveryNotify::class.java)
            .where(OfferDeliveryNotify::class.java) { message ->
                message.payload().deliveryId == deliveryId && message.payload().courierId == "courier-a"
            }
            .submit(OfferDeliveryNotify::class.java)
        val secondOffer = courierB
            .waitFor(OfferDeliveryNotify::class.java)
            .where(OfferDeliveryNotify::class.java) { message ->
                message.payload().deliveryId == deliveryId && message.payload().courierId == "courier-b"
            }
            .submit(OfferDeliveryNotify::class.java)
        val expected = listOf(
            DeliveryStatus.Assigned,
            DeliveryStatus.Reassigned,
            DeliveryStatus.Accepted,
            DeliveryStatus.Delivered,
        )
        val statuses = waitStatuses(customer, deliveryId, expected)

        val subscribed = customer
            .request(SubscribeDeliveryReq(deliveryId))
            .submit(SubscribeDeliveryRes::class.java).await()
        check(subscribed.deliveryId == deliveryId)
        println("deliverydispatch-subscribe=$deliveryId")

        val created = post(
            path = "/deliveries",
            body = CreateDeliveryReq(
                deliveryId = deliveryId,
                customerId = "customer-1",
                pickupAddress = "Kitchen 12",
                dropoffAddress = "Customer Lobby",
            ),
            responseType = CreateDeliveryRes::class.java,
        )
        check(created.deliveryId == deliveryId)
        println("deliverydispatch-create=$deliveryId")

        firstOffer.await()
        println("deliverydispatch-offer=$deliveryId:courier-a")
        val acceptedOffer = secondOffer.await().payload()
        println("deliverydispatch-offer=$deliveryId:${acceptedOffer.courierId}")
        courierB
            .send(CourierDecisionMsg(acceptedOffer.deliveryId, acceptedOffer.courierId, true, null))
            .submit()

        val notifications = statuses.waits.map { it.await().payload() }
        check(statuses.arrivals.toList() == expected)
        check(notifications.first().courierId == "courier-a")
        check(notifications.drop(1).all { it.courierId == "courier-b" })
        println(SampleNames.ReassignmentMarker)
    }

    private fun waitStatus(
        customer: ZLinkStreamConnector,
        deliveryId: String,
        status: DeliveryStatus,
    ): CompletionStage<ZLinkStreamMessage<DeliveryStatusNotify>> =
        customer
            .waitFor(DeliveryStatusNotify::class.java)
            .where(DeliveryStatusNotify::class.java) { message ->
                message.payload().deliveryId == deliveryId && message.payload().status == status
            }
            .submit(DeliveryStatusNotify::class.java)

    private fun waitStatuses(
        customer: ZLinkStreamConnector,
        deliveryId: String,
        expected: List<DeliveryStatus>,
    ): StatusWaits {
        val arrivals = Collections.synchronizedList(mutableListOf<DeliveryStatus>())
        val waits = expected.map { status ->
            waitStatus(customer, deliveryId, status).whenComplete { message, error ->
                if (error == null) {
                    arrivals.add(message.payload().status)
                }
            }
        }
        return StatusWaits(arrivals, waits)
    }

    private data class StatusWaits(
        val arrivals: MutableList<DeliveryStatus>,
        val waits: List<CompletionStage<ZLinkStreamMessage<DeliveryStatusNotify>>>,
    )

    private fun assertServerEvidence() {
        val response = post(
            path = "/self-check/assert",
            body = ServerAssertionReq("delivery-success", "delivery-reassign"),
            responseType = ServerAssertionRes::class.java,
        )
        check(response.passed)
        println(SampleNames.ServerEvidenceMarker)
    }

    private fun <TResponse> post(
        path: String,
        body: Any,
        responseType: Class<TResponse>,
    ): TResponse {
        val request = HttpRequest.newBuilder()
            .uri(URI.create(SampleTopology.DispatchHttpEndpoint + path))
            .header("content-type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(json.writeValueAsString(body)))
            .build()
        val response = http.send(request, HttpResponse.BodyHandlers.ofString())
        check(response.statusCode() in 200..299) {
            "HTTP ${response.statusCode()} for $path: ${response.body()}"
        }
        return json.readValue(response.body(), responseType)
    }

    private fun createClient(endpoint: String): ZLinkStreamConnector =
        ZLinkStreamConnectorFactory.create(
            ZLinkStreamConnectorOptions(
                URI.create(endpoint),
                ZLinkStreamDispatchMode.AUTO,
                SampleTimings.RequestTimeout,
                SampleTimings.RequestTimeout,
                2,
                Duration.ofSeconds(5),
                64 * 1024,
                64 * 1024,
                Int.MAX_VALUE,
                true,
                Duration.ofSeconds(1),
                Duration.ofSeconds(5),
                true,
                Duration.ofMillis(250),
                Duration.ofSeconds(5),
                2.0,
                false,
                null,
                null,
                null,
                null,
            ),
        )

}
