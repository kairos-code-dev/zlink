package systems.zlink.e2e.kotlin.registrymessaging.client.Scenarios

import java.security.MessageDigest
import java.util.HexFormat
import java.util.UUID
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.HttpJson
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrymessaging.shared.PayloadRes
import systems.zlink.e2e.kotlin.registrymessaging.shared.PayloadReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRes
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReq
import systems.zlink.e2e.kotlin.registrymessaging.shared.RequestFailureRes

object RmC8PayloadRoundTripScenario {
    fun run(singleConsumer: HttpJson, providerA: HttpJson, providerB: HttpJson) {
        val markers = mutableListOf<String>()
        for (size in listOf(1, 4096, 256 * 1024, 1024 * 1024)) {
            val marker = "rm-c8-$size-${UUID.randomUUID().toString().replace("-", "")}"
            markers += marker
            val payload = buildPayload(size)
            val reply = singleConsumer.post<PayloadRes>("/profile/payload", PayloadReq(marker, payload))
            ScenarioAssert.that(reply.marker == marker, "RM-C8 marker mismatch.")
            ScenarioAssert.that(reply.length == payload.length, "RM-C8 payload length mismatch.")
            ScenarioAssert.that(reply.sha256 == hash(payload), "RM-C8 payload hash mismatch.")
        }
        val oversized = singleConsumer.post<RequestFailureRes>(
            "/profile/payload-over-limit",
            PayloadReq("rm-c8-over-limit-${UUID.randomUUID()}", buildPayload(3 * 1024 * 1024)),
        )
        ScenarioAssert.that(oversized.failed, "RM-C8 oversized payload should fail.")
        ScenarioAssert.that(
            oversized.failureType == "TimeoutException",
            "RM-C8 oversized payload should report TimeoutException, got ${oversized.failureType}.",
        )
        val followUp = singleConsumer.post<ProfileRes>("/profile/request", ProfileReq("rm-c8-after"))
        ScenarioAssert.that(followUp.value == "profile:rm-c8-after", "RM-C8 follow-up request failed.")
        val evidence = providerA.get<List<String>>("/evidence") + providerB.get<List<String>>("/evidence")
        ScenarioAssert.that(
            markers.all { marker -> evidence.any { it.contains("payload-request") && it.contains(marker) } },
            "RM-C8 payload evidence missing.",
        )
        println("scenario RM-C8 max-message-size passed")
    }

    private fun buildPayload(size: Int): String =
        buildString(size) {
            for (index in 0 until size) {
                append(('a'.code + (index % 26)).toChar())
            }
        }

    private fun hash(payload: String): String =
        HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(payload.toByteArray(Charsets.UTF_8))).uppercase()
}
