package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.postAdmin
import systems.zlink.e2e.kotlin.spotservice.client.support.postJson

internal object SmC4Scenario {
    fun run() {
        val playA = Env.get("ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT")
        val gateway = Env.get("ZLINK_KOTLIN_E2E_GATEWAY_HTTP_ENDPOINT")
        val suffix = System.nanoTime().toString()
        val spotRid = "spot-sm-c4-$suffix"
        val unsubscribedSpotRid = "spot-sm-c4-unsubscribed-$suffix"
        val marker = "sm-c4-gateway"

        val created = postAdmin(playA, "/admin/create-user?rid=$spotRid")
        ensure(created.contains("\"created\":true"), "SM-C4 publish spot was not created on play-a")
        val unsubscribed = postAdmin(playA, "/admin/create-alternate?rid=$unsubscribedSpotRid")
        ensure(unsubscribed.contains("\"created\":true"), "SM-C4 unsubscribed spot was not created on play-a")

        val publish = postJson(
            gateway,
            "/spot/publish",
            mapOf("spotRid" to spotRid, "marker" to marker),
            Map::class.java
        )
        ensure(publish["scenario"] == "spot.sm-c4-publish", "SM-C4 publish operation mismatch")
        ensure(publish["rid"] == "gateway", "SM-C4 publisher was not the publish-only gateway")
        ensure(publish["spotRid"] == spotRid, "SM-C4 publish target spot mismatch")

        val snapshot = postJson(
            playA,
            "/evidence/wait",
            Contracts.EvidenceWaitReq(
                listOf("SpotMeshEvent|play-a|$spotRid|$marker"),
                10_000
            ),
            Contracts.EvidenceSnapshot::class.java
        )
        val alternateReceived = snapshot.entries.any { entry ->
            entry.marker == "SpotMeshEvent" &&
                entry.spotRid == unsubscribedSpotRid &&
                entry.value == marker
        }
        ensure(!alternateReceived, "SM-C4 unsubscribed spot received publish event")

        println("scenario SM-C4-gateway passed")
    }
}
