package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import java.time.Duration
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.client.support.expectFailure
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal object SmF4Scenario {
    fun run(outbound: ZLinkSpotOutbound) {
        expectFailure {
            outbound.requestToSpot(
                RoutingId.from("missing-route"),
                Contracts.StateReq("missing-route"),
            )
                .timeout(Duration.ofMillis(300))
                .await(Contracts.StateRes::class.java)
        }
        println("scenario SM-F4-missing-route passed")
    }
}
