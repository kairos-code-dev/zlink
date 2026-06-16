package systems.zlink.samples.kotlin.shoppingmallcheckout.server.orderworkflow.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.orderworkflow.OrderWorkflowService
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.orderworkflow.WorkflowContinuationQueue
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.StartOrderWorkflowReq
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.StartOrderWorkflowRes

@ZLinkHandlerGroup("workflow")
class StartOrderWorkflowHandler(
    private val workflow: OrderWorkflowService,
    private val continuations: WorkflowContinuationQueue,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<StartOrderWorkflowReq, StartOrderWorkflowRes> {
    override fun handle(
        request: StartOrderWorkflowReq,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        val state = workflow.start(request)
        System.err.println("shoppingmall order: started order=${request.orderId} status=${state.status}")
        continuations.enqueue(request.orderId)
        StartOrderWorkflowRes(state)
    }
}
