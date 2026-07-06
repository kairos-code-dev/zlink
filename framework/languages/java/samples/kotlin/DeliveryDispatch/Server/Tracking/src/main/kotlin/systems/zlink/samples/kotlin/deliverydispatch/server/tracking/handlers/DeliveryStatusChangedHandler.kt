package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.handlers

import systems.zlink.framework.actors.ZLinkActorClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.DeliveryEvidenceStore
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusUpdatedMsg

@ZLinkHandlerGroup("tracking")
class DeliveryStatusChangedHandler(
    private val evidenceStore: DeliveryEvidenceStore,
    private val actors: ZLinkActorClient,
) : ZLinkRequestHandler<DeliveryStatusChangedReq, DeliveryStatusChangedRes> {
    override fun handle(
        request: DeliveryStatusChangedReq,
        context: ZLinkRequestContext,
    ): DeliveryStatusChangedRes {
        evidenceStore.append(request)
        actors.sendToActor(
            "customer-1",
            DeliveryStatusUpdatedMsg(
                deliveryId = request.deliveryId,
                customerId = "customer-1",
                status = request.status,
                courierId = request.courierId,
                occurredAt = request.occurredAt,
            ),
        )
            .packetName("DeliveryStatusUpdatedMsg")
            .await()
        return DeliveryStatusChangedRes(request.deliveryId, request.status)
    }
}
