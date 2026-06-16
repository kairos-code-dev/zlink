package systems.zlink.samples.kotlin.shoppingmallcheckout.server.commerceapi.handlers

import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.shoppingmallcheckout.server.configuration.CommerceStore
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.CreatePendingMappingReq
import systems.zlink.samples.kotlin.shoppingmallcheckout.shared.contracts.CreatePendingMappingRes

/**
 * Self-check hook that seeds a pending idempotency mapping so the
 * pending-recovery scenario can prove forwarding to the owning instance.
 */
@ZLinkHandlerGroup("commerce")
class CreatePendingMappingHandler(
    private val store: CommerceStore,
) : ZLinkRequestHandler<CreatePendingMappingReq, CreatePendingMappingRes> {
    override fun handle(
        request: CreatePendingMappingReq,
        context: ZLinkRequestContext,
    ): CreatePendingMappingRes {
        store.createPendingMapping(request.idempotencyKey, request.orderId, request.ownerInstanceId)
        return CreatePendingMappingRes(true)
    }
}
