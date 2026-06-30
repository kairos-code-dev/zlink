package systems.zlink.e2e.kotlin.discoveryregistryha.provider.Handlers

import systems.zlink.e2e.kotlin.discoveryregistryha.Contracts
import systems.zlink.e2e.kotlin.discoveryregistryha.provider.Support.ProviderEvidenceStore
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class WorkRequestHandler(
    private val state: ProviderEvidenceStore,
) : ZLinkRequestHandler<Contracts.WorkRequest, Contracts.WorkReply> {
    override fun handle(
        request: Contracts.WorkRequest,
        context: ZLinkRequestContext,
    ): Contracts.WorkReply =
        Contracts.WorkReply("work:${request.value}", state.providerRid)
}
