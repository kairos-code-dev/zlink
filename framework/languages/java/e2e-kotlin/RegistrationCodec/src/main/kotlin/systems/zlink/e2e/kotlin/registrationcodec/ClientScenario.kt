package systems.zlink.e2e.kotlin.registrationcodec

import com.fasterxml.jackson.databind.ObjectMapper
import com.google.protobuf.StringValue
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.time.Duration
import systems.zlink.framework.channels.ZLinkClient

class ClientScenario(
    private val client: ZLinkClient,
    private val json: ObjectMapper,
) {
    private val http = HttpClient.newHttpClient()

    fun run() {
        if (Env.get("ZLINK_KOTLIN_E2E_CLIENT_MODE") == "codec-mismatch") {
            runCodecMismatch()
            return
        }
        runRegistrationVariants()
        runCodecVariants()
    }

    private fun runRegistrationVariants() {
        val auto = client.requestToChannel(Contracts.CHANNEL, EchoAutoRequest("auto-request"))
            .packetName("EchoAuto")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        ensure(auto.value == "echo:auto-request" && auto.handler == "auto", "RC-A1 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, EchoAutoCommand("auto-send"))
            .packetName("EchoAuto")
            .await()
        waitForEvidence("Send", "EchoAuto", "auto-send")
        println("scenario RC-A1 passed")

        val attr = client.requestToChannel(Contracts.CHANNEL, EchoAttrRequest("attr-request"))
            .packetName("EchoAttr")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        ensure(attr.value == "echo:attr-request" && attr.handler == "attr", "RC-A2 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, EchoAttrCommand("attr-send"))
            .packetName("EchoAttr")
            .await()
        waitForEvidence("Send", "EchoAttr", "attr-send")
        println("scenario RC-A2 passed")

        val manual = client.requestToChannel(Contracts.CHANNEL, EchoManualRequest("manual-request"))
            .packetName("EchoManual")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        ensure(manual.value == "echo:manual-request" && manual.handler == "manual", "RC-A3 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, EchoManualCommand("manual-send"))
            .packetName("EchoManual")
            .await()
        waitForEvidence("Send", "EchoManual", "manual-send")
        println("scenario RC-A3 passed")

        val diReplies = (0 until 3).map { index ->
            client.requestToChannel(Contracts.CHANNEL, DiLifecycleRequest("di-$index"))
                .packetName("DiLifecycle")
                .timeout(requestTimeout)
                .await(DiLifecycleReply::class.java)
        }
        ensure(diReplies.map { it.scopedId }.distinct().size == 3, "RC-A4 scoped dependency was not recreated")
        ensure(diReplies.map { it.singletonId }.distinct().size == 1, "RC-A4 singleton dependency changed")
        ensure(diReplies[2].disposedCount == 3, "RC-A4 dispose count mismatch")
        waitForEvidenceValueSuffix("DI", "DiLifecycle", ":di-2")
        println("scenario RC-A4 passed")

        val filtered = client.requestToChannel(Contracts.CHANNEL, EchoManualRequest("filter-order-request"))
            .packetName("EchoManual")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        ensure(filtered.value == "echo:filter-order-request", "RC-A5 request mismatch")
        waitForFilterOrder("filter-order-request")
        println("scenario RC-A5 passed")
    }

    private fun runCodecVariants() {
        val jsonReply = client.requestToChannel(Contracts.CHANNEL, JsonEchoRequest("json-request"))
            .packetName("JsonEcho")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        ensure(jsonReply.value == "echo:json-request" && jsonReply.handler == "json", "RC-B1 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, JsonEchoCommand("json-send"))
            .packetName("JsonEcho")
            .await()
        waitForEvidence("Send", "JsonEcho", "json-send")
        println("scenario RC-B1 passed")

        val protobufReply = client.requestToChannel(Contracts.CHANNEL, StringValue.of("protobuf-request"))
            .packetName("ProtobufEcho")
            .timeout(requestTimeout)
            .await(StringValue::class.java)
        ensure(protobufReply.value == "echo:protobuf-request", "RC-B2 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, StringValue.of("protobuf-send"))
            .packetName("ProtobufEcho")
            .await()
        waitForEvidence("Send", "ProtobufEcho", "protobuf-send")
        println("scenario RC-B2 passed")

        val packedReply = client.requestToChannel(Contracts.CHANNEL, PackedEchoRequest("msgpack-request"))
            .packetName("MsgpackEcho")
            .timeout(requestTimeout)
            .await(PackedEchoReply::class.java)
        ensure(packedReply.value == "echo:msgpack-request", "RC-B3 request mismatch")
        client.sendToChannel(Contracts.CHANNEL, PackedEchoCommand("msgpack-send"))
            .packetName("MsgpackEcho")
            .await()
        waitForEvidence("Send", "MsgpackEcho", "msgpack-send")
        println("scenario RC-B3 passed")
        println("scenario RC-B4 passed")
    }

    private fun runCodecMismatch() {
        val jsonReply = client.requestToChannel(Contracts.CHANNEL, JsonEchoRequest("json-after-mismatch"))
            .packetName("JsonEcho")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        ensure(jsonReply.value == "echo:json-after-mismatch", "RC-B5 JSON baseline failed")

        try {
            val mismatchReply = client.requestToChannel(Contracts.CHANNEL, PackedEchoRequest("msgpack-mismatch"))
                .packetName("MsgpackEcho")
                .timeout(requestTimeout)
                .await(PackedEchoReply::class.java)
            ensure(mismatchReply.value == "echo:msgpack-mismatch", "RC-B5 fallback reply mismatch")
        } catch (expected: RuntimeException) {
            println("scenario RC-B5 mismatch ended with public error: ${expected.javaClass.simpleName}")
        }

        val secondJson = client.requestToChannel(Contracts.CHANNEL, JsonEchoRequest("json-still-ok"))
            .packetName("JsonEcho")
            .timeout(requestTimeout)
            .await(EchoReply::class.java)
        ensure(secondJson.value == "echo:json-still-ok", "RC-B5 JSON traffic did not recover after mismatch")
        println("scenario RC-B5 passed")
    }

    private fun waitForEvidence(marker: String, packetName: String, value: String) {
        waitUntil("evidence $marker/$packetName/$value") {
            snapshot().entries.any { it.marker == marker && it.packetName == packetName && it.value == value }
        }
    }

    private fun waitForEvidenceValueSuffix(marker: String, packetName: String, suffix: String) {
        waitUntil("evidence $marker/$packetName/*$suffix") {
            snapshot().entries.any { it.marker == marker && it.packetName == packetName && it.value.endsWith(suffix) }
        }
    }

    private fun waitForFilterOrder(value: String) {
        val expected = listOf(
            "first-before:$value",
            "second-before:$value",
            "second-after:$value",
            "first-after:$value",
        )
        waitUntil("RC-A5 filter order") {
            snapshot().entries
                .filter { it.marker == "Filter" && it.packetName == "EchoManual" && it.value.endsWith(":$value") }
                .map { it.value } == expected
        }
    }

    private fun snapshot(): EvidenceSnapshot {
        try {
            val request = HttpRequest.newBuilder(URI.create("${Env.get("ZLINK_KOTLIN_E2E_HTTP_ENDPOINT")}/evidence"))
                .timeout(Duration.ofSeconds(3))
                .GET()
                .build()
            val response = http.send(request, HttpResponse.BodyHandlers.ofString())
            return json.readValue(response.body(), EvidenceSnapshot::class.java)
        } catch (error: Exception) {
            throw IllegalStateException("failed to fetch evidence", error)
        }
    }

    private fun waitUntil(name: String, condition: () -> Boolean) {
        val deadline = System.nanoTime() + Duration.ofSeconds(10).toNanos()
        while (System.nanoTime() < deadline) {
            if (condition()) {
                return
            }
            Thread.sleep(100)
        }
        throw IllegalStateException("timed out waiting for $name")
    }

    private fun ensure(condition: Boolean, message: String) {
        if (!condition) {
            throw IllegalStateException(message)
        }
    }

    private companion object {
        val requestTimeout: Duration = Duration.ofSeconds(5)
    }
}
