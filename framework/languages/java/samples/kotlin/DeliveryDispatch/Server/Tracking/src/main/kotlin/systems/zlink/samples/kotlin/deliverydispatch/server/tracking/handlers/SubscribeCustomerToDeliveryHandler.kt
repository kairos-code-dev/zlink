package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.actors.ZLinkActorManager
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
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
    private val spots: ZLinkSpotManager,
    private val json: ObjectMapper,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<SubscribeCustomerToDelivery, CustomerDeliverySubscribed> {
    override fun handle(
        request: SubscribeCustomerToDelivery,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        val createPart = Message.from(json.writeValueAsBytes(DeliverySpotCreate(request.deliveryId)))
        try {
            spots.getOrCreate(
                DeliveryTrackingSpot::class.java,
                RoutingId.from(request.deliveryId),
                createPart,
            ).await()
        } finally {
            createPart.close()
        }
        val actor = actors.getOrCreate(request.customerId, SampleNames.CustomerActorType).await()
        actor.context()
            .joinSpot(
                RoutingId.from(request.deliveryId),
                DeliverySpotJoin(request.deliveryId, request.customerId),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(DeliverySpotJoined::class.java)
            .await()
        CustomerDeliverySubscribed(request.customerId, request.deliveryId)
    }
}
