package systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.handlers

import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.channels.ZLinkRouteClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.spots.SpotHandleResolver
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.server.couriergateway.CourierDirectory
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.OfferDeliveryRes

@ZLinkHandlerGroup("courier-gateway")
class OfferDeliveryHandler(
    private val directory: CourierDirectory,
    private val routes: ZLinkRouteClient,
    private val spots: SpotHandleResolver,
) : ZLinkSuspendingRequestHandler<OfferDeliveryReq, OfferDeliveryRes> {
    override suspend fun handle(
        request: OfferDeliveryReq,
        context: ZLinkRequestContext,
    ): OfferDeliveryRes {
        val binding = directory.require(request.courierId)
        val nodeRid = binding.actor.nodeRid
        val spot = spots.resolveSpotHandle(nodeRid).await()
            .orElseThrow { IllegalStateException("spot not found: $nodeRid") }
        return routes
                .requestToSpot(
                    SampleNames.CourierSpotMesh,
                    spot,
                    request,
                )
                .timeout(SampleTimings.OfferRequestTimeout)
                .submit(OfferDeliveryRes::class.java)
                .await()
    }
}
