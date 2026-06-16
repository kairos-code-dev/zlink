package systems.zlink.samples.kotlin.shoppingmallcheckout.server.orderworkflow.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.orderworkflow.OrderWorkflowService
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.ContinueOrderWorkflowReq
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.ContinueOrderWorkflowRes

@ZLinkHandlerGroup("workflow")
class ContinueOrderWorkflowHandler(
    private val workflow: OrderWorkflowService,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<ContinueOrderWorkflowReq, ContinueOrderWorkflowRes> {
    override fun handle(
        request: ContinueOrderWorkflowReq,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        ContinueOrderWorkflowRes(workflow.continueWorkflow(request.orderId))
    }
}
