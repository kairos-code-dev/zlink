package systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.handlers

import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.courierspotnode.ActorDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryRes

@ZLinkHandlerGroup("courier-actor-node")
class OfferDeliveryRouteHandler(
    private val actors: ZLinkActorManager,
    private val directory: ActorDirectory,
) : ZLinkRequestHandler<OfferDeliveryReq, OfferDeliveryRes> {
    override fun handle(
        request: OfferDeliveryReq,
        context: ZLinkRequestContext,
    ): OfferDeliveryRes {
        val actor = ZLinkAwait.await(actors.getOrCreate(request.courierId, SampleNames.CourierActorType, request))
        return directory.require(actor.actorId()).offer(request)
    }
}
