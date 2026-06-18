package systems.zlink.samples.kotlin.shoppingmall.server.commerceapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.shoppingmall.server.configuration.CommerceStore
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.GetOrderStateReq
import systems.zlink.samples.kotlin.shoppingmall.shared.contracts.GetOrderStateRes

/** Read-only projection lookup. Never advances the workflow or appends events. */
@ZLinkHandlerGroup("commerce")
class GetOrderStateHandler(
    private val store: CommerceStore,
) : ZLinkRequestHandler<GetOrderStateReq, GetOrderStateRes> {
    override fun handle(
        request: GetOrderStateReq,
        context: ZLinkRequestContext,
    ): GetOrderStateRes {
        val state = store.findReadModel(request.orderId)
            ?: throw IllegalStateException("Order '${request.orderId}' does not exist.")
        return GetOrderStateRes(state)
    }
}
