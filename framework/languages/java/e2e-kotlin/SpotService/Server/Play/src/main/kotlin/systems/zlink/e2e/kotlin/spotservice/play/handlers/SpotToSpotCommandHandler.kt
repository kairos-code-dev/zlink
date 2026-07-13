package systems.zlink.e2e.kotlin.spotservice.play.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.e2e.kotlin.spotservice.Contracts
import kotlinx.coroutines.future.await
import systems.zlink.e2e.kotlin.spotservice.play.spots.UserSpot
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotPacketHandler
import systems.zlink.framework.spots.SpotHandleResolver

class SpotToSpotCommandHandler(
    private val handles: SpotHandleResolver,
) : ZLinkSuspendingSpotPacketHandler<UserSpot, Contracts.SpotToSpotCommandReq> {
    override suspend fun handle(spot: UserSpot, message: Contracts.SpotToSpotCommandReq) {
        val targetSpotRid = RoutingId.from(message.targetSpotRid)
        val target = handles.resolveSpotHandle(targetSpotRid).await()
            .orElseThrow { IllegalStateException("spot handle was not found: $targetSpotRid") }
        spot.context()
            .outbound()
            .sendToSpot(
                target,
                Contracts.OutboundMsg(message.value),
            )
            .submit()
    }
}
