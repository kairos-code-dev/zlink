package systems.zlink.samples.kotlin.supportchat.server.api.handlers

import kotlinx.coroutines.future.await
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.channels.ZLinkRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.framework.kotlin.ZLinkCoroutineRuntime
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AllocateConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AllocateConversationRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AssignAgentReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AssignAgentRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.OpenConversationApiReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.OpenConversationApiRes

@ZLinkHandlerGroup("api")
class OpenConversationHandler(
    private val client: ZLinkClient,
    private val coroutines: ZLinkCoroutineRuntime,
) : ZLinkRequestHandler<OpenConversationApiReq, OpenConversationApiRes> {
    override fun handle(
        request: OpenConversationApiReq,
        context: ZLinkRequestContext,
    ) = coroutines.blocking {
        val allocated = client
            .requestToChannel(
                SampleNames.SupportChannel,
                AllocateConversationReq(
                    request.customerActorId,
                    request.customerDisplayName,
                    request.subject,
                ),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(AllocateConversationRes::class.java)
            .await()
        val assigned = client
            .requestToChannel(
                SampleNames.SupportChannel,
                AssignAgentReq(allocated.conversationId, null),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(AssignAgentRes::class.java)
            .await()
        OpenConversationApiRes(allocated.conversationId, assigned.status)
    }
}
