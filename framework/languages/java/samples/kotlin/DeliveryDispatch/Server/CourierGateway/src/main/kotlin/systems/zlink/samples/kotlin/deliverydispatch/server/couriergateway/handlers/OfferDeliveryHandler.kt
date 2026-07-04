package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.handlers

import systems.zlink.framework.ZLinkAwait.await
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.CourierDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryRes

@ZLinkHandlerGroup("courier-gateway")
class OfferDeliveryHandler(
    private val directory: CourierDirectory,
    private val routes: ZLinkRouteClient,
) : ZLinkRequestHandler<OfferDeliveryReq, OfferDeliveryRes> {
    override fun handle(
        request: OfferDeliveryReq,
        context: ZLinkRequestContext,
    ): OfferDeliveryRes {
        val binding = directory.require(request.courierId)
        return await(
            routes
                .requestToNode(
                    SampleNames.CourierActorNodeRouteChannel,
                    binding.actor.nodeRid(),
                    request,
                )
                .timeout(SampleTimings.OfferRequestTimeout)
                .submit(OfferDeliveryRes::class.java),
        )
    }
}
