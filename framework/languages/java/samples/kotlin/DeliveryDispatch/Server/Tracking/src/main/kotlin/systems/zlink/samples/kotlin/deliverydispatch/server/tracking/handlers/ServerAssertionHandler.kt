package systems.zlink.samples.kotlin.deliverydispatch.server.tracking.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.configuration.DeliveryEvidenceStore
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.ServerAssertionRes

@ZLinkHandlerGroup("tracking")
class ServerAssertionHandler(
    private val evidenceStore: DeliveryEvidenceStore,
) : ZLinkRequestHandler<ServerAssertionReq, ServerAssertionRes> {
    override fun handle(
        request: ServerAssertionReq,
        context: ZLinkRequestContext,
    ): ServerAssertionRes =
        evidenceStore.assertSequences(
            request.successfulDeliveryId,
            request.reassignedDeliveryId,
        )
}
