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
import java.util.concurrent.locks.LockSupport
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

fun main(args: Array<String>) {
    runBlocking {
        DeliveryDispatchClientScenario().run(DeliveryDispatchClientOptions.parse(args))
    }
}

class DeliveryDispatchClientScenario {
    private val json = ObjectMapper().registerModule(KotlinModule.Builder().build())
    private val http = HttpClient.newHttpClient()

    suspend fun run(options: DeliveryDispatchClientOptions) {
        if (!options.streamRuntime) {
            options.roleUrls.forEach { (role, url) -> requireHealthy(role, url) }
        }
        println(SampleNames.TopologyReadyMarker)
        if (options.streamRuntime) {
            runStreamRuntime()
        } else {
            runScaffold(options)
        }
    }

    private fun runScaffold(options: DeliveryDispatchClientOptions) {
        val success = createDelivery(options, "delivery-success", "customer-a")
        check(success.deliveryId == "delivery-success")
        val successSubscription = subscribe(options, success.deliveryId)
        check(successSubscription.deliveryId == success.deliveryId)
        assertStatusOrder(
            deliveryId = success.deliveryId,
            expected = listOf(
                DeliveryStatus.Assigned,
                DeliveryStatus.Accepted,
                DeliveryStatus.PickedUp,
                DeliveryStatus.Delivered,
            ),
            actual = waitNotifications(options, success.deliveryId, 4),
        )

        val reassign = createDelivery(options, "delivery-reassign", "customer-b")
        check(reassign.deliveryId == "delivery-reassign")
        val reassignSubscription = subscribe(options, reassign.deliveryId)
        check(reassignSubscription.deliveryId == reassign.deliveryId)
        val reassignNotifications = waitNotifications(options, reassign.deliveryId, 4)
        assertStatusOrder(
            deliveryId = reassign.deliveryId,
            expected = listOf(
                DeliveryStatus.Assigned,
                DeliveryStatus.Reassigned,
                DeliveryStatus.Accepted,
                DeliveryStatus.Delivered,
            ),
            actual = reassignNotifications,
        )
        check(reassignNotifications
            .filter { it.status == DeliveryStatus.Accepted || it.status == DeliveryStatus.Delivered }
            .all { it.courierId == "courier-b" })
        check(readEvidence(options, "delivery-success").map { it.status } == listOf(
            DeliveryStatus.Assigned,
            DeliveryStatus.Accepted,
            DeliveryStatus.PickedUp,
            DeliveryStatus.Delivered,
        ))
        check(readEvidence(options, "delivery-reassign").map { it.status } == listOf(
            DeliveryStatus.Assigned,
            DeliveryStatus.Reassigned,
            DeliveryStatus.Accepted,
            DeliveryStatus.Delivered,
        ))

        println(SampleNames.ReassignmentMarker)
        println(SampleNames.ServerEvidenceMarker)
        println(SampleNames.CompletedMarker)
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

    private fun requireHealthy(role: String, baseUrl: String) {
        val request = HttpRequest.newBuilder(URI.create("$baseUrl/health")).GET().build()
        val response = http.send(request, HttpResponse.BodyHandlers.ofString())
        check(response.statusCode() == 200 && response.body().contains("$role=ready")) {
            "$role health check failed: ${response.statusCode()} ${response.body()}"
        }
    }

    private fun createDelivery(
        options: DeliveryDispatchClientOptions,
        deliveryId: String,
        customerId: String,
    ): CreateDeliveryRes {
        val request = CreateDeliveryReq(
            deliveryId = deliveryId,
            customerId = customerId,
            pickupAddress = "Kitchen",
            dropoffAddress = "Customer door",
        )
        val response = http.send(
            HttpRequest.newBuilder(URI.create(
                "${options.roleUrls.getValue("dispatch")}/deliveries" +
                    "?deliveryId=${request.deliveryId}&customerId=${request.customerId}",
            )).POST(HttpRequest.BodyPublishers.noBody()).build(),
            HttpResponse.BodyHandlers.ofString(),
        )
        check(response.statusCode() == 200) {
            "create delivery failed: ${response.statusCode()} ${response.body()}"
        }
        return CreateDeliveryRes(request.deliveryId)
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

    private fun subscribe(
        options: DeliveryDispatchClientOptions,
        deliveryId: String,
    ): SubscribeDeliveryRes {
        val request = SubscribeDeliveryReq(deliveryId)
        val response = http.send(
            HttpRequest.newBuilder(URI.create(
                "${options.roleUrls.getValue("customergateway")}/subscribe?deliveryId=${request.deliveryId}",
            )).POST(HttpRequest.BodyPublishers.noBody()).build(),
            HttpResponse.BodyHandlers.ofString(),
        )
        check(response.statusCode() == 200) {
            "subscribe failed: ${response.statusCode()} ${response.body()}"
        }
        return SubscribeDeliveryRes(request.deliveryId)
    }

    private fun waitNotifications(
        options: DeliveryDispatchClientOptions,
        deliveryId: String,
        count: Int,
    ): List<DeliveryStatusNotify> {
        val deadline = System.nanoTime() + 5_000_000_000L
        while (System.nanoTime() < deadline) {
            val notifications = readNotifications(options, deliveryId)
            if (notifications.size >= count) {
                return notifications
            }
            LockSupport.parkNanos(50_000_000L)
        }
        error("timed out waiting for $deliveryId notifications")
    }

    private fun readNotifications(
        options: DeliveryDispatchClientOptions,
        deliveryId: String,
    ): List<DeliveryStatusNotify> {
        val response = http.send(
            HttpRequest.newBuilder(URI.create(
                "${options.roleUrls.getValue("customergateway")}/notifications?deliveryId=$deliveryId",
            )).GET().build(),
            HttpResponse.BodyHandlers.ofString(),
        )
        check(response.statusCode() == 200) {
            "notification read failed: ${response.statusCode()} ${response.body()}"
        }
        return response.body()
            .lineSequence()
            .filter { it.isNotBlank() }
            .map { lineToNotify(it) }
            .toList()
    }

    private fun readEvidence(
        options: DeliveryDispatchClientOptions,
        deliveryId: String,
    ): List<DeliveryStatusNotify> {
        val response = http.send(
            HttpRequest.newBuilder(URI.create(
                "${options.roleUrls.getValue("tracking")}/evidence?deliveryId=$deliveryId",
            )).GET().build(),
            HttpResponse.BodyHandlers.ofString(),
        )
        check(response.statusCode() == 200) {
            "evidence read failed: ${response.statusCode()} ${response.body()}"
        }
        return response.body()
            .lineSequence()
            .filter { it.isNotBlank() }
            .map { lineToNotify(it) }
            .toList()
    }

    private fun lineToNotify(line: String): DeliveryStatusNotify {
        val parts = line.split("|")
        require(parts.size == 5) { "invalid evidence line: $line" }
        return DeliveryStatusNotify(
            deliveryId = parts[0],
            status = DeliveryStatus.valueOf(parts[2]),
            courierId = parts[3],
            occurredAt = java.time.Instant.parse(parts[4]),
        )
    }

    private fun assertStatusOrder(
        deliveryId: String,
        expected: List<DeliveryStatus>,
        actual: List<DeliveryStatusNotify>,
    ) {
        check(actual.map { it.status } == expected) {
            "$deliveryId status order mismatch: ${actual.map { it.status }}"
        }
    }
}

data class DeliveryDispatchClientOptions(
    val roleUrls: Map<String, String>,
    val streamRuntime: Boolean,
) {
    companion object {
        fun parse(args: Array<String>): DeliveryDispatchClientOptions {
            val values = mutableMapOf<String, String>()
            var index = 0
            while (index < args.size) {
                if (args[index] == "--role-url" && index + 1 < args.size) {
                    val parts = args[index + 1].split("=", limit = 2)
                    require(parts.size == 2) { "--role-url expects role=url" }
                    values[parts[0]] = parts[1]
                    index += 2
                } else if (args[index] == "--stream-runtime") {
                    index += 1
                } else {
                    index += 1
                }
            }
            val required = listOf(
                "registry",
                "tracking",
                "customergateway",
                "couriersession",
                "courierspotnode1",
                "courierspotnode2",
                "couriergateway",
                "dispatch",
            )
            if (!args.contains("--stream-runtime")) {
                required.forEach { role ->
                    require(values.containsKey(role)) { "missing --role-url for $role" }
                }
            }
            return DeliveryDispatchClientOptions(values, args.contains("--stream-runtime"))
        }
    }
}
