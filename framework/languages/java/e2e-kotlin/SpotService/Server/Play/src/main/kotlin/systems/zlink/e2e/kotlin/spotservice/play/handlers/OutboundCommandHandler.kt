package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.ScenarioState
import systems.zlink.e2e.kotlin.spotservice.play.spots.*
import systems.zlink.framework.spots.ZLinkSpotPacketHandler

class OutboundCommandHandler : ZLinkSpotPacketHandler<UserSpot, Contracts.OutboundMsg> {
    override fun handle(spot: UserSpot, message: Contracts.OutboundMsg) {
        spot.record("SpotToSpotSend", message.value)
    }
}
