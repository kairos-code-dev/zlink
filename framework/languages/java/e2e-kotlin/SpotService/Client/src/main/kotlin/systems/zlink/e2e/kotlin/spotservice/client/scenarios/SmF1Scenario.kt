package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal object SmF1Scenario {
    fun run(outbound: ZLinkSpotOutbound) {
        outbound.sendToSpot(RoutingId.from("room-a"), Contracts.StateCommand("mixed-route-send"))
            .await()
        println("scenario SM-F1 passed")
    }
}
