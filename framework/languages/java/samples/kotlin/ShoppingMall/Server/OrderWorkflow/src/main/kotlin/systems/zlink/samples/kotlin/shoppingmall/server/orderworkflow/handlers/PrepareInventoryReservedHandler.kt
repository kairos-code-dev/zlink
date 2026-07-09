package systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.OrderWorkflowService
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.PrepareInventoryReservedReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.PrepareInventoryReservedRes

/**
 * Self-check hook that stops after the inventory reservation checkpoint so the
 * client can prove explicit recovery resumes at the next event-stream step.
 */
@ZLinkHandlerGroup("workflow")
class PrepareInventoryReservedHandler(
    private val workflow: OrderWorkflowService,
) : ZLinkSuspendingRequestHandler<PrepareInventoryReservedReq, PrepareInventoryReservedRes> {
    override suspend fun handle(
        request: PrepareInventoryReservedReq,
        context: ZLinkRequestContext,
    ): PrepareInventoryReservedRes =
        PrepareInventoryReservedRes(workflow.prepareInventoryReserved(request.command))
}
