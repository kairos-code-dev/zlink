package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.client.support.REQUEST_TIMEOUT
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.eventually
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal object SmC3Scenario {
    fun run(outbound: ZLinkSpotOutbound) {
        val requestReply = eventually {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.StateReq("c3-source"),
            )
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.StateRes::class.java)
        }
        ensure(requestReply.nodeRid == "play-a", "SM-C3 source spot owner mismatch")
        outbound.sendToSpot(RoutingId.from("room-b"), Contracts.OutboundMsg("c3-send"))
            .packetName("OutboundMsg")
            .await()
        val reply = eventually {
            outbound.requestToSpot(
                RoutingId.from("room-b"),
                Contracts.OutboundReq("c3-request"),
            )
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.OutboundRes::class.java)
        }
        ensure(reply.spotRid == "room-b", "SM-C3 wrong target spot")
        ensure(reply.nodeRid == "play-b", "SM-C3 wrong target node")
        println("scenario SM-C3 passed")
    }
}
