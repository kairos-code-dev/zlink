package systems.zlink.e2e.kotlin.spotservice.client.scenarios

import java.time.Duration
import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.client.support.REQUEST_TIMEOUT
import systems.zlink.e2e.kotlin.spotservice.client.support.ensure
import systems.zlink.e2e.kotlin.spotservice.client.support.eventually
import systems.zlink.e2e.kotlin.spotservice.client.support.expectFailure
import systems.zlink.framework.spots.ZLinkSpotOutbound

internal object SmC1Scenario {
    fun runSend(outbound: ZLinkSpotOutbound) {
        outbound.sendToSpot(RoutingId.from("room-a"), Contracts.StateCommand("cmd-c1"))
            .await()
        println("scenario SM-C1-send passed")
    }

    fun runTimeout(outbound: ZLinkSpotOutbound) {
        expectFailure {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.SlowRequest("late"),
            )
                .timeout(Duration.ofMillis(100))
                .await(Contracts.StateReply::class.java)
        }
        println("scenario SM-C1-timeout passed")
    }

    fun runNormal(outbound: ZLinkSpotOutbound) {
        val after = eventually {
            outbound.requestToSpot(
                RoutingId.from("room-a"),
                Contracts.StateRequest("after-timeout"),
            )
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.StateReply::class.java)
        }
        ensure(after.value.contains("after-timeout"), "SM-C1 post-timeout request failed")
        println("scenario SM-C1 passed")
        println("scenario SM-C1-normal passed")
    }
}
