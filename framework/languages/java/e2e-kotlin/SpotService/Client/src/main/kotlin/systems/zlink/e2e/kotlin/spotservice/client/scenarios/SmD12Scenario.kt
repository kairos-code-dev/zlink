package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import java.util.UUID
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.createStreamConnector
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure

internal object SmD12Scenario {
    fun run() {
        val actorId = "actor-sm-d12-" + UUID.randomUUID().toString().replace("-", "")
        val profile = Contracts.ActorProfile("Transfer Player", 12, listOf("transfer"))
        val first = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        val second = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_B_ENDPOINT"))
        try {
            first.connect().await()
            val firstAuth = first
                .request(Contracts.ActorRemoteAuthReq(actorId, profile, "play-a"))
                .await(Contracts.ActorAuthRes::class.java)
            ensure(firstAuth.actorId == actorId, "SM-D12 first auth actor mismatch")
            ensure(firstAuth.nodeRid == "play-a", "SM-D12 first auth node mismatch")

            val firstReply = first
                .request(Contracts.ActorEchoReq("before-transfer", 1, profile))
                .metadata("actor-id", actorId)
                .await(Contracts.ActorEchoRes::class.java)
            ensure(firstReply.nodeRid == "play-a", "SM-D12 first actor node mismatch")
            ensure(firstReply.handlerSeq == 1, "SM-D12 expected initial actor state")
            first.close().await()

            second.connect().await()
            val secondAuth = second
                .request(Contracts.ActorRemoteAuthReq(actorId, profile, "play-a"))
                .await(Contracts.ActorAuthRes::class.java)
            ensure(secondAuth.actorId == actorId, "SM-D12 second auth actor mismatch")
            ensure(secondAuth.nodeRid == "play-a", "SM-D12 second auth node mismatch")

            val push = second.waitFor(Contracts.ActorPushNotify::class.java)
                .submit(Contracts.ActorPushNotify::class.java)
            val resumed = second
                .request(Contracts.ActorEchoReq("after-transfer", 2, profile))
                .metadata("actor-id", actorId)
                .await(Contracts.ActorEchoRes::class.java)
            val notify = second.await(push).payload()
            ensure(resumed.actorId == actorId, "SM-D12 resumed actor mismatch")
            ensure(resumed.nodeRid == "play-a", "SM-D12 resumed node mismatch")
            ensure(resumed.handlerSeq == 2, "SM-D12 actor state was not preserved across session servers")
            ensure(notify.actorId == actorId, "SM-D12 resumed push actor mismatch")
            ensure(notify.value == "push:after-transfer", "SM-D12 resumed push value mismatch")
            ensure(notify.handlerSeq == 2, "SM-D12 resumed push state mismatch")
            println("scenario SM-D12 passed")
        } finally {
            closeQuietly(first)
            closeQuietly(second)
        }
    }

    private fun closeQuietly(connector: systems.zlink.stream.connector.ZLinkStreamConnector) {
        try {
            connector.close().await()
        } catch (_: Exception) {
        }
    }
}
