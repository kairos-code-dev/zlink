package systems.zlink.samples.kotlin.shoppingmall.server.commerceapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.shoppingmall.server.commerceapi.OrderWorkflowRouter
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.RebuildProjectionApiReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.RebuildProjectionApiRes

/**
 * Relays projection rebuild to the OrderWorkflow owner; CommerceApi never
 * rebuilds the store itself.
 */
@ZLinkHandlerGroup("commerce")
class RebuildProjectionHandler(
    private val workflows: OrderWorkflowRouter,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<RebuildProjectionApiReq, RebuildProjectionApiRes> {
    override fun handle(
        request: RebuildProjectionApiReq,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        RebuildProjectionApiRes(workflows.rebuildProjection(request.orderId))
    }
}
