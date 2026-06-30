package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.client.support.REQUEST_TIMEOUT
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.eventually
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal object SmC2Scenario {
    fun run(outbound: ZLinkSpotOutbound) {
        val reply = eventually {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.OutboundRequest("c2"),
            )
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.OutboundReply::class.java)
        }
        ensure(reply.spotRid == "room-a", "SM-C2 wrong source spot")
        ensure(reply.nodeRid == "play-a", "SM-C2 wrong source node")
        ensure(reply.channelReply == "c2", "SM-C2 channel request reply mismatch")
        println("scenario SM-C2 passed")
    }
}
