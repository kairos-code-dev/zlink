package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.handlers

import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkSuspendingSpotRequestHandler
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.ActorDirectory
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryRes

class OfferDeliveryRouteHandler(
    private val actors: ZLinkActorManager,
    private val directory: ActorDirectory,
) : ZLinkSuspendingSpotRequestHandler<CourierEntrySpot, OfferDeliveryReq, OfferDeliveryRes> {
    override suspend fun handle(
        spot: CourierEntrySpot,
        request: OfferDeliveryReq,
    ): OfferDeliveryRes {
        val actor = actors.find(request.courierId).await()
            .orElseThrow { IllegalStateException("Courier actor is not bound: ${request.courierId}") }
        return directory.require(actor.actorId()).offer(request)
    }
}
