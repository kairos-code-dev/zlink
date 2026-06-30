package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.createStreamConnector
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.postJson

internal object SmD5Scenario {
    fun run() {
        val actorId = "actor-sm-d5-notified"
        val connector = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        try {
            val profile = Contracts.ActorProfile("Disconnect", 5, listOf("disconnect"))
            connector.connect().await()
            val auth = connector
                .request(Contracts.ActorAuthRequest(actorId, profile))
                .await(Contracts.ActorAuthReply::class.java)
            ensure(auth.actorId == actorId, "SM-D5 auth actor mismatch")
            connector.close().await()
        } finally {
            try {
                connector.close().await()
            } catch (_: Exception) {
            }
        }

        val evidence = postJson(
            Env.get("ZLINK_KOTLIN_E2E_HTTP_SESSION_ENDPOINT"),
            "/evidence/wait",
            Contracts.EvidenceWaitRequest(
                listOf(
                    "ActorDisconnectNotified",
                    "ActorEntryDisconnected|session-a|entry|$actorId",
                ),
                10_000,
            ),
            Contracts.EvidenceSnapshot::class.java,
        )
        ensure(
            evidence.entries.any { it.marker == "ActorEntryDisconnected" && it.value == actorId },
            "SM-D5 expected selected actor disconnect callback evidence",
        )

        println("scenario SM-D5 passed")
    }
}
