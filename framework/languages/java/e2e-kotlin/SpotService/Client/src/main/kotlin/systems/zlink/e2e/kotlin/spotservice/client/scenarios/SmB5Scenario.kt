package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.framework.kotlin.*

import java.time.Duration
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.createStreamConnector
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.expectFailure
import systems.zlink.e2e.kotlin.spotservice.client.support.postJson

internal object SmB5Scenario {
    suspend fun run() {
        val connector = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        try {
            val actorId = "actor-sm-b5-missing"
            val profile = Contracts.ActorProfile("Missing Handler", 5, listOf("missing"))
            connector.connect().await()
            val auth = connector
                .request(Contracts.ActorAuthReq(actorId, profile))
                .awaitReply<Contracts.ActorAuthRes>()
            ensure(auth.actorId == actorId, "SM-B5 auth actor mismatch")

            expectFailure {
                connector
                    .request(Contracts.MissingActorReq("missing-handler", 5, profile))
                    .metadata("actor-id", actorId)
                    .timeout(Duration.ofSeconds(2))
                    .awaitReply<Contracts.ActorEchoRes>()
            }

            postJson(
                Env.get("ZLINK_KOTLIN_E2E_HTTP_SESSION_ENDPOINT"),
                "/evidence/wait",
                Contracts.EvidenceWaitReq(
                    listOf("SPOT_ACTOR|HANDLER_MISSING/REPLY_ERROR/MissingActorReq"),
                    10_000,
                ),
                Contracts.EvidenceSnapshot::class.java,
            )

            println("scenario SM-B5 passed")
        } finally {
            try {
                connector.close().await()
            } catch (_: Exception) {
            }
        }
    }
}
