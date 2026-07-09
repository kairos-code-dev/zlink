package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.handlers.ZLinkSpotSubscription
import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler

@ZLinkSpotSubscription(topic = "spot.events")
class SpotEventHandler : ZLinkSpotSubscriptionHandler<UserSpot, Contracts.MeshMsg> {
    override fun handle(
        spot: UserSpot,
        message: Contracts.MeshMsg
    ) {
        spot.record("SpotMeshEvent", message.value)
    }
}
