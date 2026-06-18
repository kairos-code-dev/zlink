package systems.zlink.samples.kotlin.supportchat.client

import java.util.concurrent.TimeoutException
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import systems.zlink.framework.kotlin.ZLinkKotlinStreamConnector
import systems.zlink.framework.kotlin.await
import systems.zlink.samples.kotlin.supportchat.client.configuration.SampleNames
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AuthenticateReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.AuthenticateRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ChatMessageNotify
import systems.zlink.samples.kotlin.supportchat.shared.contracts.CloseConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.CloseConversationRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ConversationAssignedNotify
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ConversationClosedNotify
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ConversationIdleNotify
import systems.zlink.samples.kotlin.supportchat.shared.contracts.OpenConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.OpenConversationRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ParticipantJoinedNotify
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SendChatMessageReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SendChatMessageRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetAgentAvailableReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetAgentAvailableRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetTypingReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetTypingRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.TypingChangedNotify

class SupportChatClientScenario {
    suspend fun run(
        customer: ZLinkKotlinStreamConnector,
        agent: ZLinkKotlinStreamConnector,
        reconnectingCustomer: ZLinkKotlinStreamConnector,
        waitingCustomer: ZLinkKotlinStreamConnector,
        closingCustomer: ZLinkKotlinStreamConnector,
        closingAgent: ZLinkKotlinStreamConnector,
    ) = coroutineScope {
        agent.connect().await()
        val agentAuth = agent.request(AuthenticateReq("agent-1")).await<AuthenticateRes>()
        ensure(agentAuth.actorId == "agent-1")
        ensure(agentAuth.role == SampleNames.AgentRole)

        val available = agent.request(SetAgentAvailableReq(true)).await<SetAgentAvailableRes>()
        ensure(available.isAvailable)

        customer.connect().await()
        val customerAuth = customer.request(AuthenticateReq("customer-1")).await<AuthenticateRes>()
        ensure(customerAuth.actorId == "customer-1")
        ensure(customerAuth.role == SampleNames.CustomerRole)

        val assignedForAgent = async { agent.waitFor<ConversationAssignedNotify>().await() }
        val joinedForCustomer = async { customer.waitFor<ParticipantJoinedNotify>().await() }
        val opened = customer.request(OpenConversationReq("checkout payment failed")).await<OpenConversationRes>()
        ensure(opened.state.customerActorId == customerAuth.actorId)
        ensure(opened.state.status == SampleNames.WaitingForAgent || opened.state.status == SampleNames.Active)

        val joined = joinedForCustomer.await().payload()
        ensure(joined.actorId == agentAuth.actorId)
        ensure(joined.conversationId == opened.conversationId)
        ensure(joined.role == SampleNames.AgentRole)
        ensure(joined.state.status == SampleNames.Active)

        val assigned = assignedForAgent.await().payload()
        ensure(assigned.conversationId == opened.conversationId)
        ensure(assigned.state.agentActorId == agentAuth.actorId)

        val agentTypingForCustomer = async { customer.waitFor<TypingChangedNotify>().await() }
        val typing = agent.request(SetTypingReq(opened.conversationId, true)).await<SetTypingRes>()
        ensure(typing.state.status == SampleNames.Active)
        val typingPush = agentTypingForCustomer.await().payload()
        ensure(typingPush.actorId == agentAuth.actorId)
        ensure(typingPush.isTyping)

        val agentMessageForCustomer = async { customer.waitFor<ChatMessageNotify>().await() }
        val sentByAgent = agent
            .request(SendChatMessageReq(opened.conversationId, "I can help with a new payment link."))
            .await<SendChatMessageRes>()
        ensure(sentByAgent.message.messageSeq == 1L)
        ensure(sentByAgent.state.lastMessageSeq == 1L)
        val agentMessagePush = agentMessageForCustomer.await().payload()
        ensure(agentMessagePush.message.senderActorId == agentAuth.actorId)
        ensure(agentMessagePush.message.text == "I can help with a new payment link.")

        reconnectingCustomer.connect().await()
        val reconnected = reconnectingCustomer.request(AuthenticateReq("customer-1")).await<AuthenticateRes>()
        ensure(reconnected.actorId == customerAuth.actorId)

        val customerMessageForAgent = async { agent.waitFor<ChatMessageNotify>().await() }
        val sentByCustomer = reconnectingCustomer
            .request(SendChatMessageReq(opened.conversationId, "I cannot complete payment."))
            .await<SendChatMessageRes>()
        ensure(sentByCustomer.message.messageSeq == 2L)
        val customerMessagePush = customerMessageForAgent.await().payload()
        ensure(customerMessagePush.message.senderActorId == customerAuth.actorId)
        ensure(customerMessagePush.message.text == "I cannot complete payment.")

        val followUpForCustomer = async { reconnectingCustomer.waitFor<ChatMessageNotify>().await() }
        val followUpByAgent = agent
            .request(SendChatMessageReq(opened.conversationId, "Please try this replacement link."))
            .await<SendChatMessageRes>()
        ensure(followUpByAgent.message.messageSeq == 3L)
        val followUpPush = followUpForCustomer.await().payload()
        ensure(followUpPush.message.senderActorId == agentAuth.actorId)
        ensure(followUpPush.state.lastMessageSeq == 3L)

        val idleTimeout = SampleNames.IdleTimeout
            .plus(SampleNames.CloseGraceTimeout)
            .plus(SampleNames.RequestTimeout)
        val idleForCustomer = async {
            reconnectingCustomer.waitFor<ConversationIdleNotify>().timeout(idleTimeout).await()
        }
        val idleForAgent = async {
            agent.waitFor<ConversationIdleNotify>().timeout(idleTimeout).await()
        }
        val idleCustomerPush = idleForCustomer.await().payload()
        ensure(idleCustomerPush.conversationId == opened.conversationId)
        ensure(idleCustomerPush.state.status == SampleNames.WaitingForClose)
        val idleAgentPush = idleForAgent.await().payload()
        ensure(idleAgentPush.conversationId == opened.conversationId)
        ensure(idleAgentPush.state.status == SampleNames.WaitingForClose)

        val closedForAgent = async { agent.waitFor<ConversationClosedNotify>().await() }
        val closed = reconnectingCustomer
            .request(CloseConversationReq(opened.conversationId, "resolved"))
            .await<CloseConversationRes>()
        ensure(closed.state.status == SampleNames.Closed)
        val closedPush = closedForAgent.await().payload()
        ensure(closedPush.conversationId == opened.conversationId)
        ensure(closedPush.state.status == SampleNames.Closed)

        expectFailure("Closed conversation must reject follow-up messages.") {
            reconnectingCustomer
                .request(SendChatMessageReq(opened.conversationId, "after close"))
                .await<SendChatMessageRes>()
        }

        waitingCustomer.connect().await()
        val waitingCustomerAuth = waitingCustomer.request(AuthenticateReq("customer-2")).await<AuthenticateRes>()
        ensure(waitingCustomerAuth.actorId == "customer-2")
        val noAgentConversation = waitingCustomer
            .request(OpenConversationReq("agent unavailable")).await<OpenConversationRes>()
        ensure(noAgentConversation.state.status == SampleNames.WaitingForAgent)
        ensure(noAgentConversation.state.subject == "agent unavailable")
        expectNoPush<ConversationClosedNotify>(waitingCustomer, java.time.Duration.ofMillis(500))
        expectFailure("Customer actor SetAgentAvailableReq must be rejected.") {
            waitingCustomer
                .request(SetAgentAvailableReq(true))
                .await<SetAgentAvailableRes>()
        }

        closingAgent.connect().await()
        val closingAgentAuth = closingAgent.request(AuthenticateReq("agent-2")).await<AuthenticateRes>()
        ensure(closingAgentAuth.actorId == "agent-2")
        val secondAgentAvailable = closingAgent.request(SetAgentAvailableReq(true)).await<SetAgentAvailableRes>()
        ensure(secondAgentAvailable.isAvailable)

        closingCustomer.connect().await()
        val closingCustomerAuth = closingCustomer.request(AuthenticateReq("customer-3")).await<AuthenticateRes>()
        ensure(closingCustomerAuth.actorId == "customer-3")
        val explicitAssignedForAgent = async { closingAgent.waitFor<ConversationAssignedNotify>().await() }
        val explicitJoinedForCustomer = async { closingCustomer.waitFor<ParticipantJoinedNotify>().await() }
        val explicitOpened = closingCustomer.request(OpenConversationReq("explicit close")).await<OpenConversationRes>()
        val explicitJoinedPush = explicitJoinedForCustomer.await().payload()
        ensure(explicitJoinedPush.actorId == closingAgentAuth.actorId)
        ensure(explicitJoinedPush.state.status == SampleNames.Active)
        ensure(explicitAssignedForAgent.await().payload().state.agentActorId == closingAgentAuth.actorId)

        val explicitClosedForAgent = async { closingAgent.waitFor<ConversationClosedNotify>().await() }
        val explicitClosed = closingCustomer
            .request(CloseConversationReq(explicitOpened.conversationId, "done"))
            .await<CloseConversationRes>()
        ensure(explicitClosed.state.status == SampleNames.Closed)
        val explicitClosedPush = explicitClosedForAgent.await().payload()
        ensure(explicitClosedPush.conversationId == explicitOpened.conversationId)
        ensure(explicitClosedPush.state.status == SampleNames.Closed)
        expectFailure("Closed conversation must reject duplicate close.") {
            closingCustomer
                .request(CloseConversationReq(explicitOpened.conversationId, "again"))
                .await<CloseConversationRes>()
        }
    }
}

private suspend fun expectFailure(message: String, action: suspend () -> Unit) {
    try {
        action()
    } catch (error: Exception) {
        return
    }
    throw IllegalStateException(message)
}

private suspend inline fun <reified TPayload> expectNoPush(
    client: ZLinkKotlinStreamConnector,
    timeout: java.time.Duration,
) {
    try {
        client.waitFor<TPayload>().timeout(timeout).await()
    } catch (_: TimeoutException) {
        return
    }
    throw IllegalStateException("Unexpected '${TPayload::class.java.simpleName}' push was received.")
}

private fun ensure(condition: Boolean) {
    if (!condition) {
        throw IllegalStateException("Ensure failed")
    }
}
