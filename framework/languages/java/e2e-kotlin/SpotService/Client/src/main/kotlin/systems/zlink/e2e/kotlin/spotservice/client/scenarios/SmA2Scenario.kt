package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.client.support.REQUEST_TIMEOUT
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.eventually
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal object SmA2Scenario {
    fun run(outbound: ZLinkSpotOutbound) {
        val second = eventually {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.StateRequest("a2"),
            )
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.StateReply::class.java)
        }
        ensure(
            second.value.contains("a1") && second.value.contains("a2"),
            "SM-A2 state did not accumulate",
        )
        println("scenario SM-A2 passed")
    }
}
