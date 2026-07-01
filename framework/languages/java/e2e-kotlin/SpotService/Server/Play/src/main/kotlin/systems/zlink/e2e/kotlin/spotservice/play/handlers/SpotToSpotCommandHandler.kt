package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.e2e.kotlin.spotservice.play.spots.UserSpot
import systems.zlink.framework.spots.ZLinkSpotPacketHandler

class SpotToSpotCommandHandler : ZLinkSpotPacketHandler<UserSpot, Contracts.SpotToSpotCommandReq> {
    override fun handle(spot: UserSpot, message: Contracts.SpotToSpotCommandReq) {
        spot.context()
            .outbound()
            .sendToSpot(RoutingId.from(message.targetSpotRid), Contracts.OutboundMsg(message.value))
            .packetName("OutboundMsg")
            .await()
    }
}
