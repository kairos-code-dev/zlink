package systems.zlink.samples.kotlin.supportchat.shared.contracts

import systems.zlink.framework.handlers.ZLinkPacket

data class AuthenticateReq(val accessToken: String)

data class AuthenticateRes(val actorId: String, val displayName: String, val role: String)

@ZLinkPacket("AuthenticateUser")
data class AuthenticateUserReq(val accessToken: String)

data class AuthenticateUserRes(
    val accepted: Boolean,
    val actorId: String?,
    val displayName: String?,
    val role: String?,
    val reason: String?,
)

@ZLinkPacket("EnsureSupportUserActor")
data class EnsureSupportUserActorReq(
    val actorId: String,
    val displayName: String,
    val role: String,
)

data class ActorRefSnapshot(val nodeRid: ByteArray, val actorId: String, val generation: Long)

data class EnsureSupportUserActorRes(val actor: ActorRefSnapshot)

@ZLinkPacket("OpenConversationApiReq")
data class OpenConversationApiReq(
    val customerActorId: String,
    val customerDisplayName: String,
    val subject: String,
)

data class OpenConversationApiRes(val conversationId: String, val status: String)

@ZLinkPacket("AllocateConversationReq")
data class AllocateConversationReq(
    val customerActorId: String,
    val customerDisplayName: String,
    val subject: String,
)

data class AllocateConversationRes(val conversationId: String, val status: String)

@ZLinkPacket("AssignAgentReq")
data class AssignAgentReq(val conversationId: String, val requestedAgentActorId: String?)

data class AssignAgentRes(val conversationId: String, val status: String, val agentActorId: String?)

@ZLinkPacket("OpenConversationReq")
data class OpenConversationReq(val subject: String)

data class OpenConversationRes(val conversationId: String, val state: ConversationState)

@ZLinkPacket("SetAgentAvailableReq")
data class SetAgentAvailableReq(val isAvailable: Boolean)

data class SetAgentAvailableRes(val isAvailable: Boolean)

@ZLinkPacket("JoinConversationReq")
data class JoinConversationReq(val conversationId: String)

data class JoinConversationRes(val state: ConversationState)

@ZLinkPacket("SendChatMessageReq")
data class SendChatMessageReq(val conversationId: String, val text: String)

data class SendChatMessageRes(val message: ChatMessage, val state: ConversationState)

@ZLinkPacket("SetTypingReq")
data class SetTypingReq(val conversationId: String, val isTyping: Boolean)

data class SetTypingRes(val state: ConversationState)

@ZLinkPacket("CloseConversationReq")
data class CloseConversationReq(val conversationId: String, val reason: String?)

data class CloseConversationRes(val state: ConversationState)

data class ParticipantJoinedNotify(
    val conversationId: String,
    val actorId: String,
    val role: String,
    val state: ConversationState,
)

data class ConversationAssignedNotify(val conversationId: String, val state: ConversationState)

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

data class ConversationIdleNotify(val conversationId: String, val state: ConversationState)

data class ConversationClosedNotify(val conversationId: String, val state: ConversationState)

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
