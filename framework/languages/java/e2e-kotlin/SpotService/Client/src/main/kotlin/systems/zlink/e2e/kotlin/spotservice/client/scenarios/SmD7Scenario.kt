package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import java.time.Duration
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.createStreamConnector
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.expectFailure

internal object SmD7Scenario {
    fun run() {
        val preAuth = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        try {
            preAuth.connect().await()
            expectFailure {
                preAuth
                    .request(
                        Contracts.ActorEchoRequest(
                            "pre-auth-dispatch",
                            7,
                            Contracts.ActorProfile("PreAuth", 7, listOf("pre-auth")),
                        )
                    )
                    .timeout(Duration.ofMillis(500))
                    .await(Contracts.ActorEchoReply::class.java)
            }
        } finally {
            try {
                preAuth.close().await()
            } catch (_: Exception) {
            }
        }

        val authenticated = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        try {
            val profile = Contracts.ActorProfile("Auth Ok", 7, listOf("auth"))
            authenticated.connect().await()
            val auth = authenticated
                .request(Contracts.ActorAuthRequest("actor-sm-d7", profile))
                .await(Contracts.ActorAuthReply::class.java)
            ensure(auth.actorId == "actor-sm-d7", "SM-D7 auth actor mismatch")

            val push = authenticated.waitFor(Contracts.ActorPush::class.java)
                .submit(Contracts.ActorPush::class.java)
            val reply = authenticated
                .request(Contracts.ActorEchoRequest("auth-ok", 7, profile))
                .await(Contracts.ActorEchoReply::class.java)
            val notify = authenticated.await(push).payload()

            ensure(reply.actorId == "actor-sm-d7", "SM-D7 relay actor mismatch")
            ensure(reply.value == "entry:auth-ok", "SM-D7 relay value mismatch")
            ensure(notify.actorId == "actor-sm-d7", "SM-D7 push actor mismatch")
            ensure(notify.value == "push:auth-ok", "SM-D7 push value mismatch")

            println("scenario SM-D7 passed")
        } finally {
            try {
                authenticated.close().await()
            } catch (_: Exception) {
            }
        }
    }
}
