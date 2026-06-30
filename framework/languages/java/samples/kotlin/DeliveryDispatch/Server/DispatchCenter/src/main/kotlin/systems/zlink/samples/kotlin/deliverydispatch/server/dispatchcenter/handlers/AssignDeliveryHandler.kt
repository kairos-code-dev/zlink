package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchcenter.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.dispatchcenter.DispatchWorkQueue
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDeliveryReq
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDeliveryRes

@ZLinkHandlerGroup("dispatch")
class AssignDeliveryHandler(
    private val queue: DispatchWorkQueue,
) : ZLinkSuspendingRequestHandler<AssignDeliveryReq, AssignDeliveryRes> {
    override suspend fun handle(
        request: AssignDeliveryReq,
        context: ZLinkRequestContext,
    ): AssignDeliveryRes {
        queue.enqueue(request)
        System.err.println(
            "deliverydispatch dispatch: queued delivery=${request.deliveryId} customer=${request.customerId}",
        )
        return AssignDeliveryRes(request.deliveryId, "pending")
    }
}
