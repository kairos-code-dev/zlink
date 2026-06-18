package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.EvidenceStore
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.DeliveryStatuses
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionRes

@ZLinkHandlerGroup("api")
class ServerAssertionHandler(
    private val evidence: EvidenceStore,
) : ZLinkSuspendingRequestHandler<ServerAssertionReq, ServerAssertionRes> {
    override suspend fun handle(
        request: ServerAssertionReq,
        context: ZLinkRequestContext,
    ): ServerAssertionRes {
        val success = evidence.hasSequence(
            request.successfulDeliveryId,
            DeliveryStatuses.Assigned,
            DeliveryStatuses.Accepted,
            DeliveryStatuses.PickedUp,
            DeliveryStatuses.Delivered,
        )
        val reassigned = evidence.hasSequence(
            request.reassignedDeliveryId,
            DeliveryStatuses.Assigned,
            DeliveryStatuses.Reassigned,
            DeliveryStatuses.Accepted,
            DeliveryStatuses.PickedUp,
            DeliveryStatuses.Delivered,
        )
        return ServerAssertionRes(success && reassigned, evidence.readLines())
    }
}
