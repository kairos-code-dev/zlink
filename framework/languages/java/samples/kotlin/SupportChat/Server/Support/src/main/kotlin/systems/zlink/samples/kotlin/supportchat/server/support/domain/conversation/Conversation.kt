package systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation

import systems.zlink.samples.kotlin.supportchat.shared.contracts.ChatMessage
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ConversationState

class Conversation(
    private val conversationId: String,
    private val subject: String,
    val customerActorId: String,
    customerDisplayName: String,
    createdAtUnixMs: Long,
    policy: ConversationPolicy? = null,
) {
    private val policy: ConversationPolicy = policy ?: ConversationPolicy.sample()
    private val participants = LinkedHashMap<String, ConversationParticipant>()
    private val messages = mutableListOf<ConversationMessage>()
    private var status: String = Statuses.WaitingForAgent
    private var agentActorId: String? = null
    private var lastMessageSeq: Long = 0
    private var lastMessageAtUnixMs: Long? = null
    private var idleDeadlineUnixMs: Long? = null

    init {
        check(subject.isNotBlank()) { "Conversation subject is required." }
        participants[customerActorId] = ConversationParticipant(
            customerActorId,
            Roles.Customer,
            customerDisplayName,
            createdAtUnixMs,
            false,
        )
    }

    fun conversationId(): String = conversationId

    fun snapshot(): ConversationState =
        ConversationState(
            conversationId,
            subject,
            status,
            customerActorId,
            agentActorId,
            lastMessageSeq,
            lastMessageAtUnixMs,
            idleDeadlineUnixMs,
        )

    fun joinAgent(
        agentActorId: String,
        agentDisplayName: String,
        joinedAtUnixMs: Long,
    ): ConversationChange {
        ensureNotClosed("join an agent")
        check(this.agentActorId == null || this.agentActorId == agentActorId) {
            "Conversation already has an assigned agent."
        }
        this.agentActorId = agentActorId
        status = Statuses.Active
        participants[agentActorId] = ConversationParticipant(
            agentActorId,
            Roles.Agent,
            agentDisplayName,
            joinedAtUnixMs,
            false,
        )
        val state = snapshot()
        return ConversationChange(
            state,
            listOf(
                ConversationEvent(
                    ConversationEventKind.PARTICIPANT_JOINED,
                    state,
                    actorId = agentActorId,
                    role = Roles.Agent,
                ),
                ConversationEvent(
                    ConversationEventKind.ASSIGNED,
                    state,
                    actorId = agentActorId,
                    role = Roles.Agent,
                ),
            ),
        )
    }

    fun sendMessage(
        senderActorId: String,
        text: String,
        sentAtUnixMs: Long,
    ): ConversationChange {
        ensureParticipant(senderActorId)
        check(status != Statuses.Closed) { "Closed conversation cannot accept messages." }
        check(status != Statuses.WaitingForAgent) { "Conversation is waiting for an agent." }
        check(text.isNotBlank()) { "Message text is required." }
        check(text.length <= policy.maxMessageLength) { "Message text is too long." }
        status = Statuses.Active
        lastMessageSeq += 1
        lastMessageAtUnixMs = sentAtUnixMs
        idleDeadlineUnixMs = sentAtUnixMs + policy.idleTimeoutMillis
        val message = ConversationMessage(
            conversationId,
            lastMessageSeq,
            senderActorId,
            text,
            sentAtUnixMs,
        )
        messages += message
        val state = snapshot()
        return ConversationChange(
            state,
            listOf(
                ConversationEvent(
                    ConversationEventKind.MESSAGE_APPENDED,
                    state,
                    actorId = senderActorId,
                    message = toContract(message),
                ),
            ),
        )
    }

    fun setTyping(actorId: String, isTyping: Boolean): ConversationChange {
        val participant = ensureParticipant(actorId)
        ensureNotClosed("change typing state")
        participants[actorId] = participant.copy(isTyping = isTyping)
        val state = snapshot()
        return ConversationChange(
            state,
            listOf(
                ConversationEvent(
                    ConversationEventKind.TYPING_CHANGED,
                    state,
                    actorId = actorId,
                    role = participant.role,
                    isTyping = isTyping,
                ),
            ),
        )
    }

    fun markIdle(nowUnixMs: Long): ConversationChange {
        val deadline = idleDeadlineUnixMs
        if (status != Statuses.Active || deadline == null || nowUnixMs < deadline) {
            return ConversationChange(snapshot(), emptyList())
        }
        status = Statuses.WaitingForClose
        val state = snapshot()
        return ConversationChange(
            state,
            listOf(ConversationEvent(ConversationEventKind.IDLE, state)),
        )
    }

    fun close(actorId: String, reason: String?): ConversationChange {
        ensureParticipant(actorId)
        check(status != Statuses.Closed) { "Conversation is already closed." }
        status = Statuses.Closed
        val state = snapshot()
        return ConversationChange(
            state,
            listOf(ConversationEvent(ConversationEventKind.CLOSED, state, actorId = actorId)),
        )
    }

    private fun ensureParticipant(actorId: String): ConversationParticipant =
        participants[actorId] ?: throw IllegalStateException("Actor is not a conversation participant.")

    private fun ensureNotClosed(action: String) {
        check(status != Statuses.Closed) { "Closed conversation cannot $action." }
    }

    private fun toContract(message: ConversationMessage): ChatMessage =
        ChatMessage(
            message.conversationId,
            message.messageSeq,
            message.senderActorId,
            message.text,
            message.sentAtUnixMs,
        )
}
