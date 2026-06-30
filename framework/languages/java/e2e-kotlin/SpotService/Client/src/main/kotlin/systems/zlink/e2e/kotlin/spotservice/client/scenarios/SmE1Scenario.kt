package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.client.support.REQUEST_TIMEOUT
import systems.zlink.e2e.kotlin.spotservice.client.support.expectFailure
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal object SmE1Scenario {
    fun run(outbound: ZLinkSpotOutbound) {
        expectFailure {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.StateReq("missing"),
            )
                .packetName("MissingSpotPacket")
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.StateRes::class.java)
        }
        outbound.sendToSpot(RoutingId.from("room-a"), Contracts.StateMsg("missing-send"))
            .packetName("MissingSpotMsg")
            .await()
        println("scenario SM-C1-negative passed")
        println("scenario SM-E1 passed")
    }
}
