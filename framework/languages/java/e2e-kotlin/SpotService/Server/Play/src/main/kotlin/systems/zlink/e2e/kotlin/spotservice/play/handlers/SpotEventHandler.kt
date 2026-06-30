package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler

class SpotEventHandler : ZLinkSpotSubscriptionHandler<UserSpot, Contracts.MeshEvent> {
    override fun handle(
        spot: UserSpot,
        message: Contracts.MeshEvent
    ) {
        spot.record("SpotMeshEvent", message.value)
    }
}
