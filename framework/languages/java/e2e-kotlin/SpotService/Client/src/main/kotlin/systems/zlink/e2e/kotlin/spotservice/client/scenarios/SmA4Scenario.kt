package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.client.support.REQUEST_TIMEOUT
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.eventually
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal object SmA4Scenario {
    fun run(outbound: ZLinkSpotOutbound) {
        val first = eventually {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.StateReq("owner-stable-1"),
            )
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.StateRes::class.java)
        }
        val second = eventually {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.StateReq("owner-stable-2"),
            )
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.StateRes::class.java)
        }
        ensure(first.nodeRid == "play-a", "SM-A4 first owner mismatch")
        ensure(second.nodeRid == first.nodeRid, "SM-A4 owner was not stable")
        println("scenario SM-A4 passed")
    }
}
