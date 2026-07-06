package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.handlers

import systems.zlink.framework.ZLinkAwait.await
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.CourierDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryRes

@ZLinkHandlerGroup("courier-gateway")
class OfferDeliveryHandler(
    private val directory: CourierDirectory,
    private val channels: ZLinkClient,
) : ZLinkRequestHandler<OfferDeliveryReq, OfferDeliveryRes> {
    override fun handle(
        request: OfferDeliveryReq,
        context: ZLinkRequestContext,
    ): OfferDeliveryRes {
        val binding = directory.require(request.courierId)
        return await(
            channels
                .requestToChannel(
                    SampleNames.courierActorNodeChannel(binding.actor.nodeRid().toString()),
                    request,
                )
                .timeout(SampleTimings.OfferRequestTimeout)
                .submit(OfferDeliveryRes::class.java),
        )
    }
}
