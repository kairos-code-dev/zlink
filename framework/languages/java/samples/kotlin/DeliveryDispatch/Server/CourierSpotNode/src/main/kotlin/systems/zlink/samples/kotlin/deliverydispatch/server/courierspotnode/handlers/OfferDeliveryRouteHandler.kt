package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.handlers

import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.spots.ZLinkSpotRequestHandler
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.ActorDirectory
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryRes

class OfferDeliveryRouteHandler(
    private val actors: ZLinkActorManager,
    private val directory: ActorDirectory,
) : ZLinkSpotRequestHandler<CourierEntrySpot, OfferDeliveryReq, OfferDeliveryRes> {
    override fun handle(
        spot: CourierEntrySpot,
        request: OfferDeliveryReq,
    ): OfferDeliveryRes {
        val actor = ZLinkAwait.await(actors.find(request.courierId))
            .orElseThrow { IllegalStateException("Courier actor is not bound: ${request.courierId}") }
        return directory.require(actor.actorId()).offer(request)
    }
}
