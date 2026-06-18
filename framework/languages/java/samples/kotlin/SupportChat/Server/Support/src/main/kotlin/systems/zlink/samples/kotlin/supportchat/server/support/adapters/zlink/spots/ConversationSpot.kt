package systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots

import com.fasterxml.jackson.databind.ObjectMapper
import kotlinx.coroutines.future.await
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.CancellationToken
import systems.zlink.framework.kotlin.ZLinkSuspendingSpot
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse
import systems.zlink.framework.spots.ZLinkSpotContext
import systems.zlink.framework.spots.ZLinkSpotCreateResponse
import systems.zlink.framework.spots.ZLinkTimer
import systems.zlink.framework.spots.ZLinkTimerOptions
import systems.zlink.samples.kotlin.supportchat.server.configuration.SampleTimings
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.actors.SupportUserActor
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.notifications.ConversationNotificationPublisher
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers.ConversationIdleTimerHandler
import systems.zlink.samples.kotlin.supportchat.server.support.adapters.zlink.spots.handlers.ConversationSpotCreatedHandler
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.Conversation
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.ConversationPolicy
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.ConversationCreateRequest
import systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation.Roles
import systems.zlink.samples.kotlin.supportchat.shared.contracts.CloseConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.CloseConversationRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ConversationState
import systems.zlink.samples.kotlin.supportchat.shared.contracts.JoinConversationReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.JoinConversationRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SendChatMessageReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SendChatMessageRes
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetTypingReq
import systems.zlink.samples.kotlin.supportchat.shared.contracts.SetTypingRes

class ConversationSpot(
    private val context: ZLinkSpotContext,
    private val notifications: ConversationNotificationPublisher,
    private val createdHandler: ConversationSpotCreatedHandler,
    private val json: ObjectMapper,) : ZLinkSuspendingSpot<SupportUserActor>() {
    private val actors = LinkedHashMap<String, SupportUserActor>()
    private var conversation: Conversation? = null
    private var idleTimer: ZLinkTimer? = null

    override fun context(): ZLinkSpotContext = context

    override fun configure() = Unit

    override suspend fun onCreateSuspending(request: Message): ZLinkSpotCreateResponse {
        createdHandler.handle(this, request)
        return ZLinkSpotCreateResponse.accept()
    }

    override suspend fun onActorJoinSuspending(
        actor: SupportUserActor,
        request: Message,
        cancellationToken: CancellationToken,
    ): ZLinkSpotActorJoinResponse {
        val join = json.readValue(request.toByteArray(), JoinConversationReq::class.java)
        val current = requireConversation()
        if (join.conversationId != current.conversationId()) {
            return ZLinkSpotActorJoinResponse.reject()
        }
        if (Roles.Agent == actor.role) {
            val agentState = joinAgent(actor)
            return ZLinkSpotActorJoinResponse.accept(
                Message.from(json.writeValueAsBytes(JoinConversationRes(agentState))),
            )
        }
        actor.joinConversation(join.conversationId)
        actors[actor.actorId()] = actor
        if (actor.actorId() == current.customerActorId) {
            notifications.publishJoinedAgentToCustomer(actor, current.snapshot())
        }
        return ZLinkSpotActorJoinResponse.accept(
            Message.from(json.writeValueAsBytes(JoinConversationRes(current.snapshot()))),
        )
    }

    override fun onJoinedActor(actor: SupportUserActor, cancellationToken: CancellationToken) {
    }

    override fun onLeaveActor(actor: SupportUserActor, cancellationToken: CancellationToken) {
        actors.remove(actor.actorId())
    }

    override fun onDisconnectActor(actor: SupportUserActor, cancellationToken: CancellationToken) {
        actor.markDisconnected()
    }

    override suspend fun onInitializeSuspending() {
        idleTimer = context.addTimer(
            "conversation-idle",
            SampleTimings.IdleCheckPeriod,
            ConversationIdleTimerHandler::class.java,
            ZLinkTimerOptions(),
        ).await()
    }

    override suspend fun onClosingSuspending() {
        idleTimer?.cancelAsync()?.await()
    }

    fun applyCreate(request: ConversationCreateRequest) {
        conversation = Conversation(
            context.spotRid().toString(),
            request.subject,
            request.customerActorId,
            request.customerDisplayName,
            request.createdAtUnixMs,
            ConversationPolicy(
                SampleTimings.IdleTimeout.toMillis(),
                SampleTimings.CloseGraceTimeout.toMillis(),
                SampleTimings.MaxMessageLength,
            ),
        )
    }

    suspend fun joinAgent(agent: SupportUserActor): ConversationState {
        val current = requireConversation()
        val change = current.joinAgent(agent.actorId(), agent.displayName, System.currentTimeMillis())
        agent.joinConversation(current.conversationId())
        actors[agent.actorId()] = agent
        notifications.publish(change.events, actors)
        return change.state
    }

    suspend fun sendMessage(
        actor: SupportUserActor,
        request: SendChatMessageReq,
    ): SendChatMessageRes {
        ensureConversationId(request.conversationId)
        val change = requireConversation().sendMessage(
            actor.actorId(),
            request.text,
            System.currentTimeMillis(),
        )
        notifications.publish(change.events, actors)
        val message = change.events.firstNotNullOf { it.message }
        return SendChatMessageRes(message, change.state)
    }

    suspend fun setTyping(
        actor: SupportUserActor,
        request: SetTypingReq,
    ): SetTypingRes {
        ensureConversationId(request.conversationId)
        val change = requireConversation().setTyping(actor.actorId(), request.isTyping)
        notifications.publish(change.events, actors)
        return SetTypingRes(change.state)
    }

    suspend fun close(
        actor: SupportUserActor,
        request: CloseConversationReq,
    ): CloseConversationRes {
        ensureConversationId(request.conversationId)
        val change = requireConversation().close(actor.actorId(), request.reason)
        notifications.publish(change.events, actors)
        return CloseConversationRes(change.state)
    }

    suspend fun checkIdle() {
        val current = conversation ?: return
        val change = current.markIdle(System.currentTimeMillis())
        notifications.publish(change.events, actors)
    }

    private fun ensureConversationId(conversationId: String) {
        val current = requireConversation()
        check(conversationId == current.conversationId()) {
            "Request targets another conversation. conversation=$conversationId"
        }
    }

    private fun requireConversation(): Conversation =
        conversation ?: throw IllegalStateException("Conversation has not been created.")
}
