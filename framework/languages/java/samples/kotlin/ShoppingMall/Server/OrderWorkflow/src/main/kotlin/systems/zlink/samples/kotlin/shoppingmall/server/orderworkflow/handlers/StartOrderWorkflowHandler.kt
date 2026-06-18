package systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.OrderWorkflowService
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.WorkflowContinuationQueue
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.StartOrderWorkflowReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.StartOrderWorkflowRes

@ZLinkHandlerGroup("workflow")
class StartOrderWorkflowHandler(
    private val workflow: OrderWorkflowService,
    private val continuations: WorkflowContinuationQueue,
) : ZLinkSuspendingRequestHandler<StartOrderWorkflowReq, StartOrderWorkflowRes> {
    override suspend fun handle(
        request: StartOrderWorkflowReq,
        context: ZLinkRequestContext,
    ) = run {
        val state = workflow.start(request)
        System.err.println("shoppingmall order: started order=${request.orderId} status=${state.status}")
        continuations.enqueue(request.orderId)
        StartOrderWorkflowRes(state)
    }
}
