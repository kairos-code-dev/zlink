package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import java.time.Duration
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.createStreamConnector
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.expectFailure

internal object SmD4Scenario {
    fun run() {
        val connector = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        try {
            connector.connect().await()
            val profile = Contracts.ActorProfile("Multi Bind", 9, listOf("multi", "bind"))
            val bound = connector
                .request(Contracts.MultiBindRequest("actor-sm-d4-x", "actor-sm-d4-y", profile))
                .await(Contracts.MultiBindReply::class.java)
            ensure(bound.boundCount == 2, "SM-D4 expected two bound actors")

            val x = connector
                .request(Contracts.ActorEchoRequest("to-x", 10, profile))
                .metadata("actor-id", "actor-sm-d4-x")
                .await(Contracts.ActorEchoReply::class.java)
            val y = connector
                .request(Contracts.ActorEchoRequest("to-y", 11, profile))
                .metadata("actor-id", "actor-sm-d4-y")
                .await(Contracts.ActorEchoReply::class.java)
            ensure(x.actorId == "actor-sm-d4-x" && x.value == "entry:to-x", "SM-D4 x relay mismatch")
            ensure(y.actorId == "actor-sm-d4-y" && y.value == "entry:to-y", "SM-D4 y relay mismatch")

            val xPushed = connector.waitFor(Contracts.ActorPush::class.java)
                .where(Contracts.ActorPush::class.java) { it.payload().actorId == "actor-sm-d4-x" }
                .submit(Contracts.ActorPush::class.java)
            connector
                .request(Contracts.ActorEchoRequest("push-x", 12, profile))
                .metadata("actor-id", "actor-sm-d4-x")
                .await(Contracts.ActorEchoReply::class.java)
            val xPush = connector.await(xPushed).payload()
            ensure(xPush.actorId == "actor-sm-d4-x" && xPush.value == "push:push-x", "SM-D4 x push mismatch")

            val yPushed = connector.waitFor(Contracts.ActorPush::class.java)
                .where(Contracts.ActorPush::class.java) { it.payload().actorId == "actor-sm-d4-y" }
                .submit(Contracts.ActorPush::class.java)
            connector
                .request(Contracts.ActorEchoRequest("push-y", 13, profile))
                .metadata("actor-id", "actor-sm-d4-y")
                .await(Contracts.ActorEchoReply::class.java)
            val yPush = connector.await(yPushed).payload()
            ensure(yPush.actorId == "actor-sm-d4-y" && yPush.value == "push:push-y", "SM-D4 y push mismatch")

            expectFailure {
                connector
                    .request(Contracts.ActorEchoRequest("missing-actor-id", 14, profile))
                    .timeout(Duration.ofSeconds(2))
                    .await(Contracts.ActorEchoReply::class.java)
            }

            println("scenario SM-D4 passed")
        } finally {
            try {
                connector.close().await()
            } catch (_: Exception) {
            }
        }
    }
}
