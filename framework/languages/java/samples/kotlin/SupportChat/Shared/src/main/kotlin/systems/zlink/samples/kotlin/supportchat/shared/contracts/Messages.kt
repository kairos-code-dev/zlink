package systems.zlink.samples.kotlin.supportchat.shared.contracts

data class AuthenticateReq(
    val accessToken: String,
)

data class AuthenticateRes(
    val actorId: String,
    val displayName: String,
    val role: String,
)

data class AuthenticateUserReq(
    val accessToken: String,
)

data class AuthenticateUserRes(
    val accepted: Boolean,
    val actorId: String?,
    val displayName: String?,
    val role: String?,
    val reason: String?,
)

data class OpenConversationApiReq(
    val customerActorId: String,
    val customerDisplayName: String,
    val subject: String,
)

data class OpenConversationApiRes(
    val conversationId: String,
    val status: String,
)

data class AllocateConversationReq(
    val customerActorId: String,
    val customerDisplayName: String,
    val subject: String,
)

data class AllocateConversationRes(
    val conversationId: String,
    val status: String,
    val state: ConversationState? = null,
)

data class ActorRefWire(
    val nodeRid: String,
    val actorId: String,
    val generation: Long,
)

data class EnsureSupportUserActorReq(
    val actorId: String,
    val displayName: String,
    val role: String,
    val participantId: String = actorId,
)

data class EnsureSupportUserActorRes(
    val actorId: String,
    val actor: ActorRefWire,
)

data class EnsureAgentConversationReq(
    val rosterActorId: String,
    val displayName: String,
    val conversationId: String,
)

data class EnsureAgentConversationRes(
    val actor: ActorRefWire,
    val state: ConversationState,
)

data class OpenConversationReq(
    val subject: String,
)

data class OpenConversationRes(
    val conversationId: String,
    val state: ConversationState,
)

data class SetAgentAvailableReq(
    val isAvailable: Boolean,
)

data class SetAgentAvailableRes(
    val isAvailable: Boolean,
)

data class JoinConversationReq(
    val conversationId: String? = null,
)

data class JoinConversationRes(
    val state: ConversationState,
)

data class SendChatMessageReq(
    val conversationId: String? = null,
    val text: String,
)

data class SendChatMessageRes(
    val message: ChatMessage,
    val state: ConversationState,
)

data class SetTypingReq(
    val conversationId: String? = null,
    val isTyping: Boolean,
)

data class CloseConversationReq(
    val conversationId: String? = null,
    val reason: String?,
)

data class CloseConversationRes(
    val state: ConversationState,
)

data class ParticipantJoinedNotify(
    val conversationId: String,
    val actorId: String,
    val role: String,
    val state: ConversationState,
)

data class ConversationAssignedNotify(
    val conversationId: String,
    val state: ConversationState,
)

data class ChatMessageNotify(
    val conversationId: String,
    val message: ChatMessage,
    val state: ConversationState,
)

data class TypingChangedNotify(
    val conversationId: String,
    val actorId: String,
    val isTyping: Boolean,
    val state: ConversationState,
)

data class ConversationIdleNotify(
    val conversationId: String,
    val state: ConversationState,
)

data class ConversationClosedNotify(
    val conversationId: String,
    val state: ConversationState,
)

data class ConversationState(
    val conversationId: String,
    val subject: String,
    val status: String,
    val customerActorId: String,
    val agentActorId: String?,
    val lastMessageSeq: Long,
    val lastMessageAtUnixMs: Long?,
    val idleDeadlineUnixMs: Long?,
)

data class ChatMessage(
    val conversationId: String,
    val messageSeq: Long,
    val senderActorId: String,
    val text: String,
    val sentAtUnixMs: Long,
)

data class JoinConversationSupportReq(
    val conversationId: String,
    val actorId: String,
    val displayName: String,
    val role: String,
)

data class SendChatMessageSupportReq(
    val conversationId: String,
    val actorId: String,
    val text: String,
)

data class SetTypingSupportReq(
    val conversationId: String,
    val actorId: String,
    val isTyping: Boolean,
)

data class CloseConversationSupportReq(
    val conversationId: String,
    val actorId: String,
    val reason: String?,
)

data class ServerAssertionRequest(
    val conversationId: String,
)

data class ServerAssertionResponse(
    val passed: Boolean,
    val reason: String?,
)
