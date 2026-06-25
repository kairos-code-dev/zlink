package systems.zlink.e2e.kotlin.pubsub

import com.fasterxml.jackson.databind.ObjectMapper
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.file.Files
import java.nio.file.Path
import java.time.Duration
import java.util.concurrent.TimeUnit
import systems.zlink.framework.channels.ZLinkFanoutClient

class ClientScenario(
    private val fanout: ZLinkFanoutClient,
    private val json: ObjectMapper,
) {
    private val http = HttpClient.newHttpClient()

    fun run() {
        when (Env.get("ZLINK_KOTLIN_E2E_CLIENT_MODE", "default")) {
            "subscriber-restarted" -> runSubscriberRestartAfterReconnect()
            "slow-subscriber" -> runSlowSubscriberIsolation()
            "publisher-restarted" -> runPublisherRestartRecovery()
            else -> runDefault()
        }
    }

    private fun runDefault() {
        touch(Env.get("ZLINK_KOTLIN_E2E_PUBLISHER_READY_FILE"))
        waitForFile(Env.get("ZLINK_KOTLIN_E2E_PRELATE_CONTINUE_FILE"))

        publish("all", EventNotify("prelate", 0, "before-late"))
        waitForEvent("sub-1", "prelate", 0)
        waitForEvent("sub-2", "prelate", 0)
        touch(Env.get("ZLINK_KOTLIN_E2E_LATE_READY_FILE"))
        waitForFile(Env.get("ZLINK_KOTLIN_E2E_LATE_CONTINUE_FILE"))

        runFanoutBasicDelivery()
        runTopicFilter()
        runLateSubscriber()
        runMissingPacket()
    }

    private fun runSubscriberRestartAfterReconnect() {
        publish("all", EventNotify("ps-a4-down", 1, "while-sub-1-down"))
        waitForEvent("sub-2", "ps-a4-down", 1)

        waitForFile(Env.get("ZLINK_KOTLIN_E2E_LATE_CONTINUE_FILE"))

        publish("all", EventNotify("ps-a4-after", 2, "after-sub-1-restart"))
        waitForEvent("sub-1", "ps-a4-after", 2)
        waitForEvent("sub-2", "ps-a4-after", 2)
        val restarted = snapshot("sub-1")
        ensure(!hasEvent(restarted, "ps-a4-down", 1),
            "PS-A4 restarted subscriber received event from disconnected interval")
        println("scenario PS-A4 passed")
    }

    private fun runSlowSubscriberIsolation() {
        for (sequence in 0 until 8) {
            publish("all", EventNotify("ps-b1", sequence, "slow-isolation-$sequence"))
        }
        waitForEvent("sub-2", "ps-b1", 7)
        waitForEvent("sub-3", "ps-b1", 7)
        println("scenario PS-B1 passed")
    }

    private fun runPublisherRestartRecovery() {
        publish("all", EventNotify("ps-b2", 1, "after-publisher-restart"))
        waitForEvent("sub-1", "ps-b2", 1)
        waitForEvent("sub-2", "ps-b2", 1)
        waitForEvent("sub-3", "ps-b2", 1)
        println("scenario PS-B2 passed")
    }

    private fun runFanoutBasicDelivery() {
        for (index in 0 until 20) {
            publish("all", EventNotify("warmup", index, "warmup-$index"))
        }
        for (rid in listOf("sub-1", "sub-2", "sub-3")) {
            waitForAnyEvent(rid, "warmup")
        }

        for (sequence in 0 until 12) {
            publish("all", EventNotify("ps-a1", sequence, "fanout-$sequence"))
        }
        val common = commonSequences("ps-a1", setOf("sub-1", "sub-2", "sub-3"))
        ensure(hasContiguousRun(common, 4), "PS-A1 did not observe a shared contiguous sequence")
        println("scenario PS-A1 passed")
    }

    private fun runTopicFilter() {
        publish("alpha", EventNotify("ps-a2", 1, "alpha-only"))
        publish("beta", EventNotify("ps-a2", 2, "beta-only"))
        publish("gamma", EventNotify("ps-a2", 3, "gamma-only"))

        waitForEvent("sub-1", "ps-a2", 1)
        waitForEvent("sub-2", "ps-a2", 2)
        waitForEvent("sub-3", "ps-a2", 3)
        sleep(500)

        val sub1 = snapshot("sub-1")
        val sub2 = snapshot("sub-2")
        val sub3 = snapshot("sub-3")
        ensure(hasEvent(sub1, "ps-a2", 1), "PS-A2 sub-1 missed alpha")
        ensure(!hasEvent(sub1, "ps-a2", 2) && !hasEvent(sub1, "ps-a2", 3),
            "PS-A2 sub-1 recorded an uninterested topic")
        ensure(hasEvent(sub2, "ps-a2", 2), "PS-A2 sub-2 missed beta")
        ensure(!hasEvent(sub2, "ps-a2", 1) && !hasEvent(sub2, "ps-a2", 3),
            "PS-A2 sub-2 recorded an uninterested topic")
        ensure(hasEvent(sub3, "ps-a2", 3), "PS-A2 sub-3 missed gamma")
        ensure(!hasEvent(sub3, "ps-a2", 1) && !hasEvent(sub3, "ps-a2", 2),
            "PS-A2 sub-3 recorded an uninterested topic")
        println("scenario PS-A2 passed")
    }

    private fun runLateSubscriber() {
        val late = snapshot("sub-3")
        ensure(!hasEvent(late, "prelate", 0), "PS-A3 late subscriber received replayed pre-late event")
        publish("all", EventNotify("ps-a3", 1, "after-late"))
        waitForEvent("sub-3", "ps-a3", 1)
        println("scenario PS-A3 passed")
    }

    private fun runMissingPacket() {
        fanout.publish(Contracts.EVENT_CHANNEL, "all", EventNotify("ps-c1", 1, "missing-packet"))
            .packetName("MissingEventNotify")
            .await()
        waitForDispatchError("sub-1", "MissingEventNotify")
        publish("all", EventNotify("ps-c1", 2, "normal-after-missing"))
        waitForEvent("sub-1", "ps-c1", 2)
        waitForEvent("sub-2", "ps-c1", 2)
        waitForEvent("sub-3", "ps-c1", 2)
        println("scenario PS-C1 passed")
    }

    private fun publish(topic: String, message: EventNotify) {
        fanout.publish(Contracts.EVENT_CHANNEL, topic, message)
            .packetName(Contracts.EVENT_PACKET)
            .await()
    }

    private fun commonSequences(
        scenario: String,
        subscriberRids: Set<String>,
    ): List<Int> {
        val deadline = System.nanoTime() + EVIDENCE_TIMEOUT.toNanos()
        while (System.nanoTime() < deadline) {
            var common: MutableList<Int>? = null
            for (rid in subscriberRids) {
                val sequences = snapshot(rid).entries
                    .filter { it.scenario == scenario }
                    .map { it.sequence }
                    .distinct()
                    .sorted()
                if (common == null) {
                    common = sequences.toMutableList()
                } else {
                    common.retainAll(sequences.toSet())
                }
            }
            val current = common
            if (current != null && hasContiguousRun(current, 4)) {
                return current
            }
            sleep(100)
        }
        return emptyList()
    }

    private fun waitForAnyEvent(subscriberRid: String, scenario: String) {
        waitUntil {
            snapshot(subscriberRid).entries.any { it.scenario == scenario }
        }
    }

    private fun waitForEvent(
        subscriberRid: String,
        scenario: String,
        sequence: Int,
    ) {
        waitUntil { hasEvent(snapshot(subscriberRid), scenario, sequence) }
    }

    private fun waitForDispatchError(subscriberRid: String, packetName: String) {
        waitUntil {
            snapshot(subscriberRid).entries.any {
                it.marker == "DispatchError" &&
                    it.value.contains("HANDLER_MISSING") &&
                    it.value.contains("DROP") &&
                    it.value.contains(packetName)
            }
        }
    }

    private fun snapshot(subscriberRid: String): EvidenceSnapshot {
        val endpoint = when (subscriberRid) {
            "sub-1" -> Env.get("ZLINK_KOTLIN_E2E_SUB1_HTTP")
            "sub-2" -> Env.get("ZLINK_KOTLIN_E2E_SUB2_HTTP")
            "sub-3" -> Env.get("ZLINK_KOTLIN_E2E_SUB3_HTTP")
            else -> throw IllegalArgumentException("unknown subscriber $subscriberRid")
        }
        try {
            val request = HttpRequest.newBuilder(URI.create("$endpoint/evidence"))
                .timeout(Duration.ofSeconds(3))
                .GET()
                .build()
            val response = http.send(request, HttpResponse.BodyHandlers.ofString())
            return json.readValue(response.body(), EvidenceSnapshot::class.java)
        } catch (error: Exception) {
            throw IllegalStateException("failed to fetch evidence from $subscriberRid", error)
        }
    }

    private fun hasEvent(
        snapshot: EvidenceSnapshot,
        scenario: String,
        sequence: Int,
    ): Boolean =
        snapshot.entries.any {
            it.marker == "EventNotify" &&
                it.scenario == scenario &&
                it.sequence == sequence
        }

    private fun hasContiguousRun(sequences: List<Int>, minLength: Int): Boolean {
        var run = 0
        var previous = Int.MIN_VALUE
        for (value in sequences.distinct().sorted()) {
            run = if (value == previous + 1) run + 1 else 1
            if (run >= minLength) {
                return true
            }
            previous = value
        }
        return false
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
        try {
            Files.createFile(Path.of(file))
        } catch (_: java.nio.file.FileAlreadyExistsException) {
        } catch (error: Exception) {
            throw IllegalStateException("failed to create marker $file", error)
        }
    }

    private fun waitForFile(file: String) {
        if (file.isBlank()) {
            return
        }
        val path = Path.of(file)
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30)
        while (System.nanoTime() < deadline) {
            if (Files.exists(path)) {
                return
            }
            sleep(100)
        }
        throw IllegalStateException("timed out waiting for marker $file")
    }

    private fun sleep(millis: Long) {
        try {
            Thread.sleep(millis)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("interrupted", error)
        }
    }

    private fun ensure(condition: Boolean, message: String) {
        if (!condition) {
            throw IllegalStateException(message)
        }
    }

    companion object {
        private val EVIDENCE_TIMEOUT: Duration = Duration.ofSeconds(15)
    }
}
