package systems.zlink.samples.kotlin.shoppingmallcheckout.server.commerceapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.CommerceStore
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.DeleteProjectionReq
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.DeleteProjectionRes

@ZLinkHandlerGroup("commerce")
class DeleteProjectionHandler(
    private val store: CommerceStore,
) : ZLinkRequestHandler<DeleteProjectionReq, DeleteProjectionRes> {
    override fun handle(
        request: DeleteProjectionReq,
        context: ZLinkRequestContext,
    ): DeleteProjectionRes = DeleteProjectionRes(store.deleteReadModel(request.orderId))
}
