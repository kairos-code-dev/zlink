namespace SupportChat.Shared.Contracts;

public sealed record AuthenticateReq(string AccessToken);

public sealed record AuthenticateRes(
    string ActorId,
    string DisplayName,
    string Role);

public sealed record AuthenticateUserReq(string AccessToken);

public sealed record AuthenticateUserRes(
    bool Accepted,
    string? ActorId,
    string? DisplayName,
    string? Role,
    string? Reason);

public sealed record EnsureSupportUserActorReq(
    string ActorId,
    string DisplayName,
    string Role,
    string ParticipantId);

public sealed record ActorRefSnapshot(
    byte[] NodeRid,
    string ActorId,
    ulong Generation);

public sealed record EnsureSupportUserActorRes(ActorRefSnapshot Actor);

// One agent serves many conversations, so each assigned conversation gets its own
// per-conversation agent actor (ParticipantId = the agent's roster id). The session
// asks the Support server to create that actor and join it into the ConversationSpot.
public sealed record EnsureAgentConversationReq(
    string RosterActorId,
    string DisplayName,
    string ConversationId);

public sealed record EnsureAgentConversationRes(
    ActorRefSnapshot Actor,
    ConversationState State);

public sealed record OpenConversationApiReq(
    string CustomerActorId,
    string CustomerDisplayName,
    string Subject);

public sealed record OpenConversationApiRes(
    string ConversationId,
    string Status);

public sealed record AllocateConversationReq(
    string CustomerActorId,
    string CustomerDisplayName,
    string Subject);

public sealed record AllocateConversationRes(
    string ConversationId,
    string Status);

public sealed record OpenConversationReq(string Subject);

public sealed record OpenConversationRes(
    string ConversationId,
    ConversationState State);

public sealed record SetAgentAvailableReq(bool IsAvailable);

public sealed record SetAgentAvailableRes(bool IsAvailable);

// ConversationId travels as stream message metadata (§9.2), not in these bodies.
public sealed record JoinConversationReq();

public sealed record JoinConversationRes(ConversationState State);

public sealed record SendChatMessageReq(string Text);

public sealed record SendChatMessageRes(
    ChatMessage Message,
    ConversationState State);

// SetTyping is a one-way fire-and-forget send: no response record.
public sealed record SetTypingReq(bool IsTyping);

public sealed record CloseConversationReq(string? Reason);

public sealed record CloseConversationRes(ConversationState State);

public sealed record ParticipantJoinedNotify(
    string ConversationId,
    string ActorId,
    string Role,
    ConversationState State);

public sealed record ConversationAssignedNotify(
    string ConversationId,
    ConversationState State);

public sealed record ChatMessageNotify(
    string ConversationId,
    ChatMessage Message,
    ConversationState State);

public sealed record TypingChangedNotify(
    string ConversationId,
    string ActorId,
    bool IsTyping,
    ConversationState State);

public sealed record ConversationIdleNotify(
    string ConversationId,
    ConversationState State);

public sealed record ConversationClosedNotify(
    string ConversationId,
    ConversationState State);

public sealed record ConversationState(
    string ConversationId,
    string Subject,
    string Status,
    string CustomerActorId,
    string? AgentActorId,
    ulong LastMessageSeq,
    long? LastMessageAtUnixMs,
    long? IdleDeadlineUnixMs);

public sealed record ChatMessage(
    string ConversationId,
    ulong MessageSeq,
    string SenderActorId,
    string Text,
    long SentAtUnixMs);