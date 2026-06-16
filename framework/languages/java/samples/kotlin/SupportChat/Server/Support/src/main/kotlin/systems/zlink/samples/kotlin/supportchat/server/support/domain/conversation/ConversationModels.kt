package systems.zlink.samples.kotlin.supportchat.server.support.domain.conversation

import systems.zlink.samples.kotlin.supportchat.server.configuration.ConversationStatuses
import systems.zlink.samples.kotlin.supportchat.server.configuration.SupportChatRoles
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ChatMessage
import systems.zlink.samples.kotlin.supportchat.shared.contracts.ConversationState

object Statuses {
    const val WaitingForAgent: String = ConversationStatuses.WaitingForAgent
    const val Active: String = ConversationStatuses.Active
    const val WaitingForClose: String = ConversationStatuses.WaitingForClose
    const val Closed: String = ConversationStatuses.Closed
}

object Roles {
    const val Customer: String = SupportChatRoles.Customer
    const val Agent: String = SupportChatRoles.Agent
}

data class ConversationPolicy(
    val idleTimeoutMillis: Long,
    val closeGraceTimeoutMillis: Long,
    val maxMessageLength: Int,
) {
    companion object {
        fun sample(): ConversationPolicy = ConversationPolicy(3000, 2000, 500)
    }
}

data class ConversationParticipant(
    val actorId: String,
    val role: String,
    val displayName: String,
    val joinedAtUnixMs: Long,
    val isTyping: Boolean,
)

data class ConversationMessage(
    val conversationId: String,
    val messageSeq: Long,
    val senderActorId: String,
    val text: String,
    val sentAtUnixMs: Long,
)

enum class ConversationEventKind {
    PARTICIPANT_JOINED,
    ASSIGNED,
    MESSAGE_APPENDED,
    TYPING_CHANGED,
    IDLE,
    CLOSED,
}

data class ConversationEvent(
    val kind: ConversationEventKind,
    val state: ConversationState,
    val actorId: String? = null,
    val role: String? = null,
    val message: ChatMessage? = null,
    val isTyping: Boolean? = null,
)

data class ConversationChange(
    val state: ConversationState,
    val events: List<ConversationEvent>,
)

data class ConversationCreateRequest(
    val customerActorId: String = "",
    val customerDisplayName: String = "",
    val subject: String = "",
    val createdAtUnixMs: Long = 0,
)
