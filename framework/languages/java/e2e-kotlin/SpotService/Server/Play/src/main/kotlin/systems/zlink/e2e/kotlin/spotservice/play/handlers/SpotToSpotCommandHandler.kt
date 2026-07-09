package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import systems.zlink.framework.locations.SpotRef
import systems.zlink.e2e.kotlin.spotservice.play.spots.UserSpot
import systems.zlink.framework.spots.ZLinkSpotPacketHandler

class SpotToSpotCommandHandler : ZLinkSpotPacketHandler<UserSpot, Contracts.SpotToSpotCommandReq> {
    override fun handle(spot: UserSpot, message: Contracts.SpotToSpotCommandReq) {
        val targetSpotRid = RoutingId.from(message.targetSpotRid)
        val targetNode = if (message.targetSpotRid.contains("b")) "play-b" else "play-a"
        spot.context()
            .outbound()
            .sendToSpot(
                SpotRef(Contracts.ROUTE_CHANNEL, RoutingId.from(targetNode), targetSpotRid),
                Contracts.OutboundMsg(message.value),
            )
            .packetName("OutboundMsg")
            .await()
    }
}
