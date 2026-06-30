package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.handlers.ZLinkSpotRequest

class SlowRequestHandler {
    @ZLinkSpotRequest
    fun handle(
        spot: UserSpot,
        request: Contracts.SlowRequest,
    ): Contracts.StateReply {
        try {
            Thread.sleep(1000)
        } catch (error: InterruptedException) {
            Thread.currentThread().interrupt()
            throw IllegalStateException("interrupted", error)
        }
        return Contracts.StateReply(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            spot.apply("slow:${request.value}"),
        )
    }
}
