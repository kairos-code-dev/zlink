package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import java.util.UUID
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.createStreamConnector
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.postJson

internal object SmB6Scenario {
    fun run() {
        val suffix = UUID.randomUUID().toString().replace("-", "")
        val leaveActorId = "actor-sm-b6-left-$suffix"
        val disconnectActorId = "actor-sm-b6-disconnected-$suffix"
        val profile = Contracts.ActorProfile("Leave", 6, listOf("leave"))

        val leaveClient = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        try {
            leaveClient.connect().await()
            val auth = leaveClient
                .request(Contracts.ActorAuthReq(leaveActorId, profile))
                .await(Contracts.ActorAuthRes::class.java)
            ensure(auth.actorId == leaveActorId, "SM-B6 leave auth actor mismatch")

            val joined = leaveClient
                .request(Contracts.ActorJoinReq("room-a", profile, profile.tags))
                .metadata("actor-id", leaveActorId)
                .await(Contracts.ActorJoinRes::class.java)
            ensure(joined.actorId == leaveActorId, "SM-B6 leave join actor mismatch")

            val left = leaveClient
                .request(Contracts.LeaveActorReq(leaveActorId))
                .metadata("actor-id", leaveActorId)
                .await(Contracts.LeaveActorRes::class.java)
            ensure(left.accepted && left.actorId == leaveActorId, "SM-B6 leave reply mismatch")
        } finally {
            try {
                leaveClient.close().await()
            } catch (_: Exception) {
            }
        }

        val leaveEvidence = postJson(
            Env.get("ZLINK_KOTLIN_E2E_HTTP_SESSION_ENDPOINT"),
            "/evidence/wait",
            Contracts.EvidenceWaitReq(
                listOf("ActorUserLeft|session-a|room-a|$leaveActorId"),
                10_000,
            ),
            Contracts.EvidenceSnapshot::class.java,
        )
        ensure(
            leaveEvidence.entries.none {
                it.marker == "ActorUserDisconnected" &&
                    it.spotRid == "room-a" &&
                    it.value == leaveActorId
            },
            "SM-B6 explicit leave emitted disconnect evidence",
        )

        val disconnectClient = createStreamConnector(Env.get("ZLINK_KOTLIN_E2E_STREAM_A_ENDPOINT"))
        try {
            val disconnectProfile = Contracts.ActorProfile("Disconnect", 6, listOf("disconnect"))
            disconnectClient.connect().await()
            val auth = disconnectClient
                .request(Contracts.ActorAuthReq(disconnectActorId, disconnectProfile))
                .await(Contracts.ActorAuthRes::class.java)
            ensure(auth.actorId == disconnectActorId, "SM-B6 disconnect auth actor mismatch")
            val joined = disconnectClient
                .request(Contracts.ActorJoinReq("room-a", disconnectProfile, disconnectProfile.tags))
                .metadata("actor-id", disconnectActorId)
                .await(Contracts.ActorJoinRes::class.java)
            ensure(joined.actorId == disconnectActorId, "SM-B6 disconnect join actor mismatch")
            disconnectClient.close().await()
        } finally {
            try {
                disconnectClient.close().await()
            } catch (_: Exception) {
            }
        }

        val disconnectEvidence = postJson(
            Env.get("ZLINK_KOTLIN_E2E_HTTP_SESSION_ENDPOINT"),
            "/evidence/wait",
            Contracts.EvidenceWaitReq(
                listOf("ActorUserDisconnected|session-a|room-a|$disconnectActorId"),
                10_000,
            ),
            Contracts.EvidenceSnapshot::class.java,
        )
        ensure(
            disconnectEvidence.entries.none {
                it.marker == "ActorUserLeft" &&
                    it.value == disconnectActorId
            },
            "SM-B6 disconnect emitted leave evidence",
        )

        println("scenario SM-B6 passed")
    }
}
