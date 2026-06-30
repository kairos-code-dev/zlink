package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import java.time.Duration
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.createStreamConnector
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.postJson

internal object SmD8Scenario {
    fun run() {
        val actorId = "actor-sm-d8-reconnect"
        val profile = Contracts.ActorProfile("Reconnect", 8, listOf("reconnect"))
        val first = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        try {
            first.connect().await()
            val auth = first
                .request(Contracts.ActorAuthReq(actorId, profile))
                .await(Contracts.ActorAuthRes::class.java)
            ensure(auth.actorId == actorId, "SM-D8 initial auth actor mismatch")

            val pending = first
                .request(Contracts.SlowSessionReq("before-disconnect", 1000))
                .timeout(Duration.ofSeconds(10))
                .submit(Contracts.SlowSessionRes::class.java)
            Thread.sleep(100)
            first.close().await()

            var pendingFailed = false
            try {
                first.await(pending)
            } catch (_: Exception) {
                pendingFailed = true
            }
            ensure(pendingFailed, "SM-D8 expected pending request to fail after stream disconnect")
        } finally {
            try {
                first.close().await()
            } catch (_: Exception) {
            }
        }

        postJson(
            Env.get("ZLINK_KOTLIN_E2E_HTTP_SESSION_ENDPOINT"),
            "/evidence/wait",
            Contracts.EvidenceWaitReq(
                listOf("ActorEntryDisconnected|session-a|entry|$actorId"),
                10_000,
            ),
            Contracts.EvidenceSnapshot::class.java,
        )

        val second = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        try {
            second.connect().await()
            val auth = second
                .request(Contracts.ActorAuthReq(actorId, profile))
                .await(Contracts.ActorAuthRes::class.java)
            ensure(auth.actorId == actorId, "SM-D8 reauth actor mismatch")

            val reply = second
                .request(Contracts.ActorEchoReq("after-reconnect", 8, profile))
                .await(Contracts.ActorEchoRes::class.java)
            ensure(reply.actorId == actorId, "SM-D8 reconnected actor mismatch")
            ensure(reply.nodeRid == "session-a", "SM-D8 reconnected node mismatch")
            ensure(reply.value == "entry:after-reconnect", "SM-D8 reconnected value mismatch")

            println("scenario SM-D8 passed")
        } finally {
            try {
                second.close().await()
            } catch (_: Exception) {
            }
        }
    }
}
