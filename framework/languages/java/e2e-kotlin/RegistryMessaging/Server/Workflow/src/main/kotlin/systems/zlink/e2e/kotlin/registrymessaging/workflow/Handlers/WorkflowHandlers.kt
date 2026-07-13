package systems.zlink.e2e.kotlin.registrymessaging.workflow.Handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.e2e.kotlin.registrymessaging.shared.WorkflowRes
import systems.zlink.e2e.kotlin.registrymessaging.shared.WorkflowReq
import systems.zlink.e2e.kotlin.registrymessaging.workflow.Infrastructure.EvidenceStore
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent
import systems.zlink.framework.configuration.ZLinkMessageFlowObserver
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler

class WorkflowRequestHandler(
    private val evidence: EvidenceStore,
) : ZLinkSuspendingRequestHandler<WorkflowReq, WorkflowRes> {
    override suspend fun handle(request: WorkflowReq, context: ZLinkRequestContext): WorkflowRes {
        evidence.add("workflow-request|rid=${evidence.rid}|value=${request.value}|packet=${context.packetName()}")
        return WorkflowRes("workflow:${request.value}", evidence.rid)
    }
}

class EvidenceDispatchErrorObserver(
    private val evidence: EvidenceStore,
) : ZLinkMessageFlowObserver {
    override fun onMessageFlow(flow: ZLinkMessageFlowEvent): CompletionStage<Void> {
        if (flow.outcome() != ZLinkMessageFlowOutcome.ERROR) {
            return CompletableFuture.completedFuture(null)
        }
        evidence.add(
            "dispatch-error" +
                "|surface=${flow.surface()}" +
                "|kind=${flow.messageKind()}" +
                "|reason=${flow.errorReason()}" +
                "|action=${flow.errorAction()}" +
                "|packet=${flow.packetName() ?: "<null>"}" +
                "|channel=${flow.channelName() ?: "<null>"}",
        )
        return CompletableFuture.completedFuture(null)
    }
}
