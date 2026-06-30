package systems.zlink.e2e.kotlin.runtimemonitoring.failoverservice

import systems.zlink.e2e.kotlin.runtimemonitoring.Contracts
import systems.zlink.e2e.kotlin.runtimemonitoring.service.EvidenceState
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup

@ZLinkHandlerGroup(Contracts.HANDLER_GROUP)
class FailoverWorkRequestHandler(
    private val state: EvidenceState,
) : ZLinkRequestHandler<Contracts.WorkRequest, Contracts.WorkReply> {
    override fun handle(
        request: Contracts.WorkRequest,
        context: ZLinkRequestContext,
    ): Contracts.WorkReply {
        state.record("handler", Contracts.CHANNEL, "FailoverRequest", request.value)
        return Contracts.WorkReply("work:${request.value}", "svc-a")
    }
}
