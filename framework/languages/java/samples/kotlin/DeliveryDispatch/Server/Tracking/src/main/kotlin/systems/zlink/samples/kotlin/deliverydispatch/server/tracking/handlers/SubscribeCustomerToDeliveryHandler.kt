package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.deliverytrackingspot.DeliveryTrackingSpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeCustomerToDeliveryRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotCreateReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeCustomerToDeliveryReq

@ZLinkHandlerGroup("tracking")
class SubscribeCustomerToDeliveryHandler(
    private val actors: ZLinkActorManager,
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
) : ZLinkSuspendingRequestHandler<SubscribeCustomerToDeliveryReq, SubscribeCustomerToDeliveryRes> {
    override suspend fun handle(
        request: SubscribeCustomerToDeliveryReq,
        context: ZLinkRequestContext,
    ) = run {
        spots.getOrCreate(
            DeliveryTrackingSpot::class.java,
            RoutingId.from(request.deliveryId),
            DeliverySpotCreateReq(request.deliveryId),
        ).await()
        val actor = actors.getOrCreate(request.customerId, SampleNames.CustomerActorType).await()
        require(actor.actorId().isNotBlank())
        SubscribeCustomerToDeliveryRes(request.customerId, request.deliveryId)
    }
}
