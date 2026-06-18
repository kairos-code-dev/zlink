package systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.OrderWorkflowService
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.RebuildOrderProjectionReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.RebuildOrderProjectionRes

@ZLinkHandlerGroup("workflow")
class RebuildOrderProjectionHandler(
    private val workflow: OrderWorkflowService,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<RebuildOrderProjectionReq, RebuildOrderProjectionRes> {
    override fun handle(
        request: RebuildOrderProjectionReq,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        val state = workflow.rebuildProjection(request.orderId)
        System.err.println("shoppingmall projection: rebuilt order=${request.orderId} status=${state.status}")
        RebuildOrderProjectionRes(state)
    }
}
