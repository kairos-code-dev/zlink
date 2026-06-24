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
    string Role);

public sealed record ActorRefSnapshot(
    byte[] NodeRid,
    string ActorId,
    ulong Generation);

public sealed record EnsureSupportUserActorRes(ActorRefSnapshot Actor);

public sealed record OpenConversationApiReq(
    string CustomerActorId,
    string CustomerDisplayName,
    string Subject);

public sealed record OpenConversationApiRes(
    string ConversationId,
    string Status,
    string? AgentActorId);

public sealed record AllocateConversationReq(
    string CustomerActorId,
    string CustomerDisplayName,
    string Subject);

public sealed record AllocateConversationRes(
    string ConversationId,
    string Status);

public sealed record AssignAgentReq(
    string ConversationId,
    string? RequestedAgentActorId);

public sealed record AssignAgentRes(
    string ConversationId,
    string Status,
    string? AgentActorId);

public sealed record OpenConversationReq(string Subject);

public sealed record OpenConversationRes(
    string ConversationId,
    ConversationState State);

public sealed record SetAgentAvailableReq(bool IsAvailable);

public sealed record SetAgentAvailableRes(bool IsAvailable);

public sealed record JoinConversationReq(string ConversationId);

public sealed record JoinConversationRes(ConversationState State);

public sealed record SendChatMessageReq(
    string ConversationId,
    string Text);

public sealed record SendChatMessageRes(
    ChatMessage Message,
    ConversationState State);

public sealed record SetTypingReq(
    string ConversationId,
    bool IsTyping);

public sealed record SetTypingRes(ConversationState State);

public sealed record CloseConversationReq(
    string ConversationId,
    string? Reason);

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
