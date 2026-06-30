package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.handlers

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.contracts.core.RoutingId
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.EvidenceStore
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.SampleNames
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.deliverytrackingspot.DeliverySpotDirectory
import systems.zlink.samples.kotlin.deliverydispatch.server.tracking.spots.deliverytrackingspot.DeliveryTrackingSpot
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliverySpotCreateReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusNotify

@ZLinkHandlerGroup("tracking")
class DeliveryStatusChangedHandler(
    private val spots: ZLinkSpotManager,
    private val directory: DeliverySpotDirectory,
    private val evidence: EvidenceStore,
    private val fanout: ZLinkFanoutClient,
    private val json: ObjectMapper,
) : ZLinkSuspendingRequestHandler<DeliveryStatusChangedReq, DeliveryStatusChangedRes> {
    override suspend fun handle(
        request: DeliveryStatusChangedReq,
        context: ZLinkRequestContext,
    ) = run {
        spots.getOrCreate(
            DeliveryTrackingSpot::class.java,
            RoutingId.from(request.deliveryId),
            DeliverySpotCreateReq(request.deliveryId),
        ).await()
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
        DeliveryStatusChangedRes(request.deliveryId, request.status)
    }
}
