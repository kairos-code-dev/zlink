package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.handlers

import systems.zlink.framework.kotlin.await
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.actors.ZLinkActorClient
import systems.zlink.framework.actors.ZLinkActorDirectory
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.DeliveryEvidenceStore
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedRes
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusChangedReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatusUpdatedMsg

@ZLinkHandlerGroup("tracking")
class DeliveryStatusChangedHandler(
    private val evidenceStore: DeliveryEvidenceStore,
    private val actors: ZLinkActorClient,
    private val actorRefs: ZLinkActorDirectory,
) : ZLinkSuspendingRequestHandler<DeliveryStatusChangedReq, DeliveryStatusChangedRes> {
    override suspend fun handle(
        request: DeliveryStatusChangedReq,
        context: ZLinkRequestContext,
    ): DeliveryStatusChangedRes {
        evidenceStore.append(request)
        val actorRef = actorRefs.find("customer-1").await()
            .orElseThrow { IllegalStateException("customer actor not found: customer-1") }
        actors.sendToActor(
            actorRef,
            DeliveryStatusUpdatedMsg(
                deliveryId = request.deliveryId,
                customerId = "customer-1",
                status = request.status,
                courierId = request.courierId,
                occurredAt = request.occurredAt,
            ),
        )
            .submit()
        return DeliveryStatusChangedRes(request.deliveryId, request.status)
    }
}
