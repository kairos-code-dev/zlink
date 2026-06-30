package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.client.support.REQUEST_TIMEOUT
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.eventually
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal object SmA3Scenario {
    fun run(outbound: ZLinkSpotOutbound) {
        val roomA = eventually {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.StateReq("owner-a"),
            )
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.StateRes::class.java)
        }
        ensure(roomA.nodeRid == "play-a", "SM-A3 room-a owner mismatch")
        println("scenario SM-A3 passed")
    }
}
