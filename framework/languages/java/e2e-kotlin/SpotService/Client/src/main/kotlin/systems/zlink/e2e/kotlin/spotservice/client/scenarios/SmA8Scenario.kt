package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.Env
import systems.zlink.e2e.kotlin.spotservice.client.support.REQUEST_TIMEOUT
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.eventually
import systems.zlink.e2e.kotlin.spotservice.client.support.waitForEvidence
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal object SmA8Scenario {
    fun run(outbound: ZLinkSpotOutbound) {
        eventually {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.StateRequest("worker-start-long"),
            )
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.StateReply::class.java)
        }
        val followUp = eventually {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.StateRequest("worker-follow-up"),
            )
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.StateReply::class.java)
        }
        ensure(followUp.value.contains("worker-follow-up"), "SM-A8 follow-up state was not applied")
        val evidenceEndpoint = Env.get("ZLINK_KOTLIN_E2E_HTTP_A_ENDPOINT")
        waitForEvidence(evidenceEndpoint, "WorkerFollowUpBeforeComplete")
        waitForEvidence(evidenceEndpoint, "WorkerCompleted")
        println("scenario SM-A8 passed")
    }
}
