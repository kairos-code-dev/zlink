package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.actors.ZLinkActorGateway
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.DeliveryTrackingSpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.CustomerDeliverySubscribed
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotCreate
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotJoin
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotJoined
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.SubscribeCustomerToDelivery

@ZLinkHandlerGroup("tracking")
class SubscribeCustomerToDeliveryHandler(
    private val actors: ZLinkActorManager,
    private val actorGateway: ZLinkActorGateway,
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
) : ZLinkSuspendingRequestHandler<SubscribeCustomerToDelivery, CustomerDeliverySubscribed> {
    override suspend fun handle(
        request: SubscribeCustomerToDelivery,
        context: ZLinkRequestContext,
    ) = run {
        spots.getOrCreate(
            DeliveryTrackingSpot::class.java,
            RoutingId.from(request.deliveryId),
            DeliverySpotCreate(request.deliveryId),
        ).await()
        val actor = actors.getOrCreate(request.customerId, SampleNames.CustomerActorType).await()
        actorGateway
            .joinSpot(
                actor,
                RoutingId.from(request.deliveryId),
                DeliverySpotJoin(request.deliveryId, request.customerId),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(DeliverySpotJoined::class.java)
            .await()
        CustomerDeliverySubscribed(request.customerId, request.deliveryId)
    }
}
