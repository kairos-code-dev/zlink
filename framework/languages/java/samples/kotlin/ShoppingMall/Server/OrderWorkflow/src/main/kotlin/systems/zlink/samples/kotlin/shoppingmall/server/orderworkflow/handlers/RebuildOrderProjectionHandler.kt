package systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.shoppingmall.server.orderworkflow.OrderWorkflowService
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.RebuildOrderProjectionReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.RebuildOrderProjectionRes

@ZLinkHandlerGroup("workflow")
class RebuildOrderProjectionHandler(
    private val workflow: OrderWorkflowService,
) : ZLinkSuspendingRequestHandler<RebuildOrderProjectionReq, RebuildOrderProjectionRes> {
    override suspend fun handle(
        request: RebuildOrderProjectionReq,
        context: ZLinkRequestContext,
    ) = run {
        val state = workflow.rebuildProjection(request.orderId)
        System.err.println("shoppingmall projection: rebuilt order=${request.orderId} status=${state.status}")
        RebuildOrderProjectionRes(state)
    }
}
