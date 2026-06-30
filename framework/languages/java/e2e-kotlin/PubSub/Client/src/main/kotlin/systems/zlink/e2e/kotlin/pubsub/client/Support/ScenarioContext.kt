package systems.zlink.e2e.kotlin.pubsub.client.Support

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.module.kotlin.readValue
import java.net.URI
import java.net.URLEncoder
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.file.Files
import java.nio.file.Path
import java.time.Duration
import systems.zlink.e2e.kotlin.pubsub.client.Scenarios.FanoutBasicDeliveryScenario
import systems.zlink.e2e.kotlin.pubsub.client.Scenarios.LateSubscriberScenario
import systems.zlink.e2e.kotlin.pubsub.client.Scenarios.MissingMessageNameScenario
import systems.zlink.e2e.kotlin.pubsub.client.Scenarios.PublisherRestartScenario
import systems.zlink.e2e.kotlin.pubsub.client.Scenarios.SlowSubscriberScenario
import systems.zlink.e2e.kotlin.pubsub.client.Scenarios.SubscriberReconnectScenario
import systems.zlink.e2e.kotlin.pubsub.client.Scenarios.TopicFilterScenario
import systems.zlink.e2e.kotlin.pubsub.shared.EventMsg
import systems.zlink.e2e.kotlin.pubsub.shared.EvidenceSnapshot

class ScenarioContext(
    private val options: ClientOptions,
    private val json: ObjectMapper,
) {
    private val http = HttpClient.newHttpClient()
    private val publisherUrl = options.publisherHttp

    fun run() {
        when (options.mode) {
            "subscriber-restarted" -> SubscriberReconnectScenario.run(this)
            "slow-subscriber" -> SlowSubscriberScenario.run(this)
            "publisher-restarted" -> PublisherRestartScenario.run(this)
            else -> runDefault()
        }
    }

    private fun runDefault() {
        touch(options.publisherReadyFile)
        waitForFile(options.prelateContinueFile)

        publish("all", EventMsg("prelate", 0, "before-late"))
        waitForEvent("sub-1", "prelate", 0)
        waitForEvent("sub-2", "prelate", 0)
        touch(options.lateReadyFile)
        waitForFile(options.lateContinueFile)

        FanoutBasicDeliveryScenario.run(this)
        TopicFilterScenario.run(this)
        LateSubscriberScenario.run(this)
        MissingMessageNameScenario.run(this)
    }

    fun runSubscriberRestartAfterReconnect() {
        publish("all", EventMsg("ps-a4-down", 1, "while-sub-1-down"))
        waitForEvent("sub-2", "ps-a4-down", 1)

        waitForFile(options.lateContinueFile)

        publish("all", EventMsg("ps-a4-after", 2, "after-sub-1-restart"))
        waitForEvent("sub-1", "ps-a4-after", 2)
        waitForEvent("sub-2", "ps-a4-after", 2)
        val restarted = snapshot("sub-1")
        ensure(
            !hasEvent(restarted, "ps-a4-down", 1),
            "PS-A4 restarted subscriber received event from disconnected interval",
        )
        println("scenario PS-A4 passed")
    }

    fun runSlowSubscriberIsolation() {
        for (sequence in 0 until 8) {
            publish("all", EventMsg("ps-b1", sequence, "slow-isolation-$sequence"))
        }
        waitForEvent("sub-2", "ps-b1", 7)
        waitForEvent("sub-3", "ps-b1", 7)
        println("scenario PS-B1 passed")
    }

    fun runPublisherRestartRecovery() {
        publish("all", EventMsg("ps-b2", 1, "after-publisher-restart"))
        waitForEvent("sub-1", "ps-b2", 1)
        waitForEvent("sub-2", "ps-b2", 1)
        waitForEvent("sub-3", "ps-b2", 1)
        println("scenario PS-B2 passed")
    }

    fun runFanoutBasicDelivery() {
        for (index in 0 until 20) {
            publish("all", EventMsg("warmup", index, "warmup-$index"))
        }
        for (rid in listOf("sub-1", "sub-2", "sub-3")) {
            waitForAnyEvent(rid, "warmup")
        }

        for (sequence in 0 until 12) {
            publish("all", EventMsg("ps-a1", sequence, "fanout-$sequence"))
        }
        for (rid in listOf("sub-1", "sub-2", "sub-3")) {
            for (sequence in 0 until 4) {
                waitForEvent(rid, "ps-a1", sequence)
            }
        }
        println("scenario PS-A1 passed")
    }

    fun runTopicFilter() {
        publish("alpha", EventMsg("ps-a2", 1, "alpha-only"))
        publish("beta", EventMsg("ps-a2", 2, "beta-only"))
        publish("gamma", EventMsg("ps-a2", 3, "gamma-only"))

        waitForEvent("sub-1", "ps-a2", 1)
        waitForEvent("sub-2", "ps-a2", 2)
        waitForEvent("sub-3", "ps-a2", 3)
        sleep(500)

        val sub1 = snapshot("sub-1")
        val sub2 = snapshot("sub-2")
        val sub3 = snapshot("sub-3")
        ensure(hasEvent(sub1, "ps-a2", 1), "PS-A2 sub-1 missed alpha")
        ensure(
            !hasEvent(sub1, "ps-a2", 2) && !hasEvent(sub1, "ps-a2", 3),
            "PS-A2 sub-1 recorded an uninterested topic",
        )
        ensure(hasEvent(sub2, "ps-a2", 2), "PS-A2 sub-2 missed beta")
        ensure(
            !hasEvent(sub2, "ps-a2", 1) && !hasEvent(sub2, "ps-a2", 3),
            "PS-A2 sub-2 recorded an uninterested topic",
        )
        ensure(hasEvent(sub3, "ps-a2", 3), "PS-A2 sub-3 missed gamma")
        ensure(
            !hasEvent(sub3, "ps-a2", 1) && !hasEvent(sub3, "ps-a2", 2),
            "PS-A2 sub-3 recorded an uninterested topic",
        )
        println("scenario PS-A2 passed")
    }

    fun runLateSubscriber() {
        val late = snapshot("sub-3")
        ensure(!hasEvent(late, "prelate", 0), "PS-A3 late subscriber received replayed pre-late event")
        publish("all", EventMsg("ps-a3", 1, "after-late"))
        waitForEvent("sub-3", "ps-a3", 1)
        println("scenario PS-A3 passed")
    }

    fun runMissingPacket() {
        publishMissing("all", EventMsg("ps-c1", 1, "missing-packet"))
        waitForDispatchError("sub-1", "MissingEventMsg")
        publish("all", EventMsg("ps-c1", 2, "normal-after-missing"))
        waitForEvent("sub-1", "ps-c1", 2)
        waitForEvent("sub-2", "ps-c1", 2)
        waitForEvent("sub-3", "ps-c1", 2)
        println("scenario PS-C1 passed")
    }

    private fun publish(topic: String, message: EventMsg) {
        postPublisher("/publish", PublishReq(topic, message))
    }

    private fun publishMissing(topic: String, message: EventMsg) {
        postPublisher("/publish-missing", PublishReq(topic, message))
    }

    private fun postPublisher(path: String, body: PublishReq) {
        val request = HttpRequest.newBuilder(URI.create("$publisherUrl$path"))
            .timeout(Duration.ofSeconds(5))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofByteArray(json.writeValueAsBytes(body)))
            .build()
        val response = http.send(request, HttpResponse.BodyHandlers.ofString())
        ensure(response.statusCode() in 200..299, "publisher returned HTTP ${response.statusCode()}: ${response.body()}")
    }

    private fun waitForAnyEvent(subscriberRid: String, scenario: String) {
        waitForEvidence(subscriberRid, marker = "EventMsg", scenario = scenario)
    }

    private fun waitForEvent(
        subscriberRid: String,
        scenario: String,
        sequence: Int,
    ) {
        waitForEvidence(subscriberRid, marker = "EventMsg", scenario = scenario, sequence = sequence)
    }

    private fun waitForDispatchError(subscriberRid: String, packetName: String) {
        waitForEvidence(
            subscriberRid,
            marker = "DispatchError",
            valueContains = "HANDLER_MISSING/DROP/$packetName",
        )
    }

    private fun waitForEvidence(
        subscriberRid: String,
        marker: String,
        scenario: String? = null,
        sequence: Int? = null,
        valueContains: String? = null,
    ): EvidenceSnapshot {
        val endpoint = subscriberEndpoint(subscriberRid)
        val query = buildList {
            add("marker=${encode(marker)}")
            scenario?.let { add("scenario=${encode(it)}") }
            sequence?.let { add("sequence=$it") }
            valueContains?.let { add("contains=${encode(it)}") }
            add("timeoutMs=${EVIDENCE_TIMEOUT.toMillis()}")
        }.joinToString("&")
        val request = HttpRequest.newBuilder(URI.create("$endpoint/evidence/wait?$query"))
            .timeout(EVIDENCE_TIMEOUT.plusSeconds(5))
            .GET()
            .build()
        val response = http.send(request, HttpResponse.BodyHandlers.ofString())
        ensure(
            response.statusCode() in 200..299,
            "subscriber $subscriberRid evidence wait failed with HTTP ${response.statusCode()}: ${response.body()}",
        )
        return json.readValue(response.body())
    }

    private fun snapshot(subscriberRid: String): EvidenceSnapshot {
        val endpoint = subscriberEndpoint(subscriberRid)
        val request = HttpRequest.newBuilder(URI.create("$endpoint/evidence"))
            .timeout(Duration.ofSeconds(3))
            .GET()
            .build()
        val response = http.send(request, HttpResponse.BodyHandlers.ofString())
        return json.readValue(response.body())
    }

    private fun subscriberEndpoint(subscriberRid: String): String =
        when (subscriberRid) {
            "sub-1" -> options.sub1Http
            "sub-2" -> options.sub2Http
            "sub-3" -> options.sub3Http
            else -> throw IllegalArgumentException("unknown subscriber $subscriberRid")
        }

    private fun hasEvent(
        snapshot: EvidenceSnapshot,
        scenario: String,
        sequence: Int,
    ): Boolean =
        snapshot.entries.any {
            it.marker == "EventMsg" &&
                it.scenario == scenario &&
                it.sequence == sequence
        }

    private fun waitUntil(check: () -> Boolean) {
        val deadline = System.nanoTime() + EVIDENCE_TIMEOUT.toNanos()
        var last: Throwable? = null
        while (System.nanoTime() < deadline) {
            try {
                if (check()) {
                    return
                }
            } catch (error: Throwable) {
                last = error
            }
            sleep(100)
        }
        throw IllegalStateException("timed out waiting for evidence", last)
    }

    private fun touch(file: String) {
        if (file.isBlank()) {
            return
        }
        Files.writeString(Path.of(file), "ready")
    }

    private fun waitForFile(file: String) {
        if (file.isBlank()) {
            return
        }
        waitUntil { Files.exists(Path.of(file)) }
    }

    private fun ensure(condition: Boolean, message: String) {
        if (!condition) {
            throw IllegalStateException(message)
        }
    }

    private fun sleep(milliseconds: Long) {
        try {
            Thread.sleep(milliseconds)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("sleep interrupted", error)
        }
    }

    private fun encode(value: String): String =
        URLEncoder.encode(value, Charsets.UTF_8)

    private class PublishReq() {
        var topic: String = ""
        var message: EventMsg = EventMsg()

        constructor(topic: String, message: EventMsg) : this() {
            this.topic = topic
            this.message = message
        }
    }

    companion object {
        private val EVIDENCE_TIMEOUT: Duration = Duration.ofSeconds(30)
    }
}
