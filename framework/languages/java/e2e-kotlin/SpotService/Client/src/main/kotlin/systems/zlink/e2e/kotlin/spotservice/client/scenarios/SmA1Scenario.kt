package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.client.support.REQUEST_TIMEOUT
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.eventually
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal object SmA1Scenario {
    fun run(outbound: ZLinkSpotOutbound) {
        val first = eventually {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.StateRequest("a1"),
            )
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.StateReply::class.java)
        }
        ensure(first.spotRid == "room-a", "SM-A1 wrong spot rid")
        ensure(first.nodeRid == "play-a", "SM-A1 wrong owner node")
        println("scenario SM-A1 passed")
    }
}
