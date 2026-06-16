package systems.zlink.samples.kotlin.deliverydispatch.server.dispatchcenter.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.deliverydispatch.server.dispatchcenter.DispatchWorkQueue
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDelivery
import systems.zlink.samples.kotlin.deliverydispatch.shared.contracts.AssignDeliveryResult

@ZLinkHandlerGroup("dispatch")
class AssignDeliveryHandler(
    private val queue: DispatchWorkQueue,
) : ZLinkRequestHandler<AssignDelivery, AssignDeliveryResult> {
    override fun handle(
        request: AssignDelivery,
        context: ZLinkRequestContext,
    ): AssignDeliveryResult {
        queue.enqueue(request)
        System.err.println(
            "deliverydispatch dispatch: queued delivery=${request.deliveryId} customer=${request.customerId}",
        )
        return AssignDeliveryResult(request.deliveryId, "pending")
    }
}
