package systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.OrderWorkflowService
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.ContinueOrderWorkflowReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.ContinueOrderWorkflowRes

@ZLinkHandlerGroup("workflow")
class ContinueOrderWorkflowHandler(
    private val workflow: OrderWorkflowService,
) : ZLinkSuspendingRequestHandler<ContinueOrderWorkflowReq, ContinueOrderWorkflowRes> {
    override suspend fun handle(
        request: ContinueOrderWorkflowReq,
        context: ZLinkRequestContext,
    ) = run {
        ContinueOrderWorkflowRes(workflow.continueWorkflow(request.orderId))
    }
}
