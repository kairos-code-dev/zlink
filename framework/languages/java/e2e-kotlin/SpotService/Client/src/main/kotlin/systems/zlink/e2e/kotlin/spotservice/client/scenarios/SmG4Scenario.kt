package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import java.util.UUID
import java.util.concurrent.CompletableFuture
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.createStreamConnector
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.stream.connector.ZLinkStreamConnector

internal object SmG4Scenario {
    fun run() {
        val connectors = mutableListOf<ZLinkStreamConnector>()
        val actorIds = mutableListOf<String>()
        val values = mutableListOf<String>()
        try {
            repeat(8) { index ->
                val actorId = "actor-sm-g4-$index-" + UUID.randomUUID().toString().replace("-", "")
                val profile = Contracts.ActorProfile("Bound Load $index", 14, listOf("load", "session-$index"))
                val connector = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
                connectors += connector
                actorIds += actorId
                values += "push-$index"
                connector.connect().await()
                val auth = connector
                    .request(Contracts.ActorAuthReq(actorId, profile))
                    .await(Contracts.ActorAuthRes::class.java)
                ensure(auth.actorId == actorId, "SM-G4 auth actor mismatch")
            }

            val pushes = connectors.map { connector ->
                connector.waitFor(Contracts.ActorPushNotify::class.java)
                    .submit(Contracts.ActorPushNotify::class.java)
            }
            val replies = connectors.mapIndexed { index, connector ->
                val value = values[index]
                val profile = Contracts.ActorProfile("Bound Load Request $index", 14, listOf("load"))
                CompletableFuture.supplyAsync {
                    connector
                        .request(Contracts.ActorEchoReq(value, 14, profile))
                        .await(Contracts.ActorEchoRes::class.java)
                }
            }

            connectors.forEachIndexed { index, connector ->
                val actorId = actorIds[index]
                val value = values[index]
                val reply = replies[index].join()
                val notify = connector.await(pushes[index]).payload()
                ensure(reply.actorId == actorId, "SM-G4 push reply actor mismatch")
                ensure(reply.value == "entry:$value", "SM-G4 push reply value mismatch")
                ensure(notify.actorId == actorId, "SM-G4 push notify actor mismatch")
                ensure(notify.value == "push:$value", "SM-G4 push notify value mismatch")
            }

            println("scenario SM-G4 passed")
        } finally {
            connectors.forEach { connector ->
                try {
                    connector.close().await()
                } catch (_: Exception) {
                }
            }
        }
    }
}
