package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import java.time.Duration
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.awaitUnchecked
import systems.zlink.e2e.kotlin.spotservice.client.support.createStreamConnector
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.expectFailure

internal object SmD6Scenario {
    fun run() {
        val bound = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        val shadow = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_B_ENDPOINT"))
        try {
            val boundProfile = Contracts.ActorProfile("Bound", 6, listOf("bound"))
            val shadowProfile = Contracts.ActorProfile("Shadow", 6, listOf("shadow"))
            bound.connect().await()
            bound
                .request(Contracts.ActorAuthRequest("actor-sm-d6", boundProfile))
                .await(Contracts.ActorAuthReply::class.java)
            shadow.connect().await()
            shadow
                .request(Contracts.ActorAuthRequest("actor-sm-d6-shadow", shadowProfile))
                .await(Contracts.ActorAuthReply::class.java)

            val shadowPush = shadow.waitFor(Contracts.ActorPush::class.java)
                .timeout(Duration.ofMillis(400))
                .submit(Contracts.ActorPush::class.java)
            val boundPush = bound.waitFor(Contracts.ActorPush::class.java)
                .submit(Contracts.ActorPush::class.java)
            val reply = bound
                .request(Contracts.ActorEchoRequest("push-bound-only", 20, boundProfile))
                .await(Contracts.ActorEchoReply::class.java)
            val notify = bound.await(boundPush).payload()

            ensure(reply.actorId == "actor-sm-d6", "SM-D6 reply actor mismatch")
            ensure(notify.actorId == "actor-sm-d6", "SM-D6 push actor mismatch")
            ensure(notify.value == "push:push-bound-only", "SM-D6 push value mismatch")
            expectFailure { awaitUnchecked(shadow, shadowPush) }
            ensure(shadow.receivedCount("ActorPush") == 0, "SM-D6 shadow session received push")

            println("scenario SM-D6 passed")
        } finally {
            try {
                bound.close().await()
            } catch (_: Exception) {
            }
            try {
                shadow.close().await()
            } catch (_: Exception) {
            }
        }
    }
}
