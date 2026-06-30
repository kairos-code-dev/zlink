package systems.zlink.e2e.kotlin.registrymessaging.workflow.Handlers

import java.util.concurrent.CompletableFuture
import java.util.concurrent.CompletionStage
import systems.zlink.e2e.kotlin.registrymessaging.shared.WorkflowReply
import systems.zlink.e2e.kotlin.registrymessaging.shared.WorkflowRequest
import systems.zlink.e2e.kotlin.registrymessaging.workflow.Infrastructure.EvidenceStore
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent
import systems.zlink.framework.configuration.ZLinkMessageFlowObserver
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome

class WorkflowRequestHandler(
    private val evidence: EvidenceStore,
) : ZLinkRequestHandler<WorkflowRequest, WorkflowReply> {
    override fun handle(request: WorkflowRequest, context: ZLinkRequestContext): WorkflowReply {
        evidence.add("workflow-request|rid=${evidence.rid}|value=${request.value}|packet=${context.packetName()}")
        return WorkflowReply("workflow:${request.value}", evidence.rid)
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
