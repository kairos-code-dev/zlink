using SupportChat.Shared.Contracts;

namespace SupportChat.Server.Support.Domain.SupportChat;

internal sealed record ConversationPolicy(
    TimeSpan IdleTimeout,
    TimeSpan CloseGraceTimeout,
    int MaxMessageLength)
{
    public static ConversationPolicy Sample { get; } = new(
        TimeSpan.FromSeconds(3),
        TimeSpan.FromSeconds(2),
        500);
}

internal sealed record ConversationParticipant(
    string ActorId,
    string Role,
    string DisplayName,
    long JoinedAtUnixMs,
    bool IsTyping);

internal sealed record ConversationMessage(
    string ConversationId,
    ulong MessageSeq,
    string SenderActorId,
    string Text,
    long SentAtUnixMs);

internal enum ConversationEventKind
{
    ParticipantJoined,
    Assigned,
    MessageAppended,
    TypingChanged,
    Idle,
    Closed,
}

internal sealed record ConversationEvent(
    ConversationEventKind Kind,
    ConversationState State,
    string? ActorId = null,
    string? Role = null,
    ChatMessage? Message = null,
    bool? IsTyping = null);

internal sealed record ConversationChange(
    ConversationState State,
    IReadOnlyList<ConversationEvent> Events);
