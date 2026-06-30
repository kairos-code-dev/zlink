package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.handlers.ZLinkSpotRequest

class StateRequestHandler {
    @ZLinkSpotRequest
    fun handle(
        spot: UserSpot,
        request: Contracts.StateRequest,
    ): Contracts.StateReply {
        val value = if (request.op == "worker-start" || request.op == "worker-start-long") {
            spot.startWorker(request.op)
        } else {
            spot.apply(request.op)
        }
        return Contracts.StateReply(
            spot.context().spotRid().toString(),
            spot.context().nodeRid().toString(),
            value,
        )
    }
}
