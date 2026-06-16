package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.EvidenceStore
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.DeliverySpotDirectory
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.DeliveryTrackingSpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotCreate
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusAck
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChanged
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusNotify

@ZLinkHandlerGroup("tracking")
class DeliveryStatusChangedHandler(
    private val spots: ZLinkSpotManager,
    private val directory: DeliverySpotDirectory,
    private val evidence: EvidenceStore,
    private val fanout: ZLinkFanoutClient,
    private val json: ObjectMapper,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<DeliveryStatusChanged, DeliveryStatusAck> {
    override fun handle(
        request: DeliveryStatusChanged,
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
        evidence.append(request)
        directory.require(request.deliveryId).record(request)
        fanout.publish(
            SampleNames.StatusFanoutChannel,
            request.deliveryId,
            DeliveryStatusNotify(
                request.deliveryId,
                request.status,
                request.courierId,
                request.occurredAtUnixMs,
            ),
        )
            .submit()
            .await()
        System.err.println(
            "deliverydispatch tracking: status delivery=${request.deliveryId} status=${request.status} courier=${request.courierId}",
        )
        DeliveryStatusAck(request.deliveryId, request.status)
    }
}
