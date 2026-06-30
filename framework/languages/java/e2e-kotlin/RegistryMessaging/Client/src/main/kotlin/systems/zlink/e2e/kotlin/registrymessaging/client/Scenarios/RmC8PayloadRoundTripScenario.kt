package systems.zlink.e2e.kotlin.registrymessaging.client.Scenarios

import java.security.MessageDigest
import java.util.HexFormat
import java.util.UUID
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.HttpJson
import systems.zlink.e2e.kotlin.registrymessaging.client.Support.ScenarioAssert
import systems.zlink.e2e.kotlin.registrymessaging.shared.PayloadReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.PayloadRequest
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.ProfileRequest

object RmC8PayloadRoundTripScenario {
    fun run(singleConsumer: HttpJson, providerA: HttpJson) {
        val before = providerA.get<List<String>>("/evidence")
        val markers = mutableListOf<String>()
        for (size in listOf(1, 4096, 256 * 1024, 1024 * 1024)) {
            val marker = "rm-c8-$size-${UUID.randomUUID().toString().replace("-", "")}"
            markers += marker
            val payload = buildPayload(size)
            val reply = singleConsumer.post<PayloadReply>("/profile/payload", PayloadRequest(marker, payload))
            ScenarioAssert.that(reply.marker == marker, "RM-C8 marker mismatch.")
            ScenarioAssert.that(reply.length == payload.length, "RM-C8 payload length mismatch.")
            ScenarioAssert.that(reply.sha256 == hash(payload), "RM-C8 payload hash mismatch.")
        }
        val followUp = singleConsumer.post<ProfileReply>("/profile/request", ProfileRequest("rm-c8-after"))
        ScenarioAssert.that(followUp.value == "profile:rm-c8-after", "RM-C8 follow-up request failed.")
        val after = providerA.get<List<String>>("/evidence")
        ScenarioAssert.that(
            markers.all { ScenarioAssert.countNewEvidence(after, before, "payload-request|rid=api-a", it) == 1 },
            "RM-C8 payload evidence missing.",
        )
        println("scenario RM-C8 passed")
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
