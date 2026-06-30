package systems.zlink.e2e.kotlin.discoveryregistryha.provider.Handlers

import systems.zlink.e2e.kotlin.discoveryregistryha.Contracts
import systems.zlink.e2e.kotlin.discoveryregistryha.provider.Support.ProviderEvidenceStore
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class WorkRequestHandler(
    private val state: ProviderEvidenceStore,
) : ZLinkRequestHandler<Contracts.WorkReq, Contracts.WorkRes> {
    override fun handle(
        request: Contracts.WorkReq,
        context: ZLinkRequestContext,
    ): Contracts.WorkRes =
        Contracts.WorkRes("work:${request.value}", state.providerRid)
}
