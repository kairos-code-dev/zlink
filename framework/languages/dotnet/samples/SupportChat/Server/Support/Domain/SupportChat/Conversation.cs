using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;

namespace SupportChat.Server.Support.Domain.SupportChat;

internal sealed class Conversation
{
    private readonly List<ConversationMessage> _messages = [];
    private readonly Dictionary<string, ConversationParticipant> _participants = new(StringComparer.Ordinal);
    private readonly ConversationPolicy _policy;
    private long? _idleDeadlineUnixMs;
    private long? _lastMessageAtUnixMs;
    private ulong _lastMessageSeq;

    public Conversation(
        string conversationId,
        string subject,
        string customerActorId,
        string customerDisplayName,
        long createdAtUnixMs,
        ConversationPolicy? policy = null)
    {
        if (string.IsNullOrWhiteSpace(subject))
            throw new InvalidOperationException("Conversation subject is required.");

        ConversationId = conversationId;
        Subject = subject;
        CustomerActorId = customerActorId;
        Status = ConversationStatuses.WaitingForAgent;
        _policy = policy ?? ConversationPolicy.Sample;
        _participants[customerActorId] = new ConversationParticipant(
            customerActorId,
            SupportChatRoles.Customer,
            customerDisplayName,
            createdAtUnixMs,
            false);
    }

    public string ConversationId { get; }

    public string Subject { get; }

    public string Status { get; private set; }

    public string CustomerActorId { get; }

    public string? AgentActorId { get; private set; }

    public ConversationState Snapshot()
    {
        return new ConversationState(
            ConversationId,
            Subject,
            Status,
            CustomerActorId,
            AgentActorId,
            _lastMessageSeq,
            _lastMessageAtUnixMs,
            _idleDeadlineUnixMs);
    }

    public ConversationChange JoinAgent(
        string agentActorId,
        string agentDisplayName,
        long joinedAtUnixMs)
    {
        EnsureNotClosed("join an agent");
        if (AgentActorId is not null && !string.Equals(AgentActorId, agentActorId, StringComparison.Ordinal))
            throw new InvalidOperationException("Conversation already has an assigned agent.");

        AgentActorId = agentActorId;
        Status = ConversationStatuses.Active;
        _participants[agentActorId] = new ConversationParticipant(
            agentActorId,
            SupportChatRoles.Agent,
            agentDisplayName,
            joinedAtUnixMs,
            false);

        var state = Snapshot();
        return new ConversationChange(
            state,
            [
                new ConversationEvent(
                    ConversationEventKind.ParticipantJoined,
                    state,
                    agentActorId,
                    SupportChatRoles.Agent),
                new ConversationEvent(
                    ConversationEventKind.Assigned,
                    state,
                    agentActorId,
                    SupportChatRoles.Agent)
            ]);
    }

    public ConversationChange SendMessage(
        string senderActorId,
        string text,
        long sentAtUnixMs)
    {
        EnsureParticipant(senderActorId);
        if (Status == ConversationStatuses.Closed)
            throw new InvalidOperationException("Closed conversation cannot accept messages.");
        if (Status == ConversationStatuses.WaitingForAgent)
            throw new InvalidOperationException("Conversation is waiting for an agent.");
        if (string.IsNullOrWhiteSpace(text)) throw new InvalidOperationException("Message text is required.");
        if (text.Length > _policy.MaxMessageLength) throw new InvalidOperationException("Message text is too long.");

        Status = ConversationStatuses.Active;
        _lastMessageSeq += 1;
        _lastMessageAtUnixMs = sentAtUnixMs;
        _idleDeadlineUnixMs = sentAtUnixMs + (long)_policy.IdleTimeout.TotalMilliseconds;
        var message = new ConversationMessage(
            ConversationId,
            _lastMessageSeq,
            senderActorId,
            text,
            sentAtUnixMs);
        _messages.Add(message);

        var state = Snapshot();
        return new ConversationChange(
            state,
            [
                new ConversationEvent(
                    ConversationEventKind.MessageAppended,
                    state,
                    senderActorId,
                    Message: ToContract(message))
            ]);
    }

    public ConversationChange SetTyping(
        string actorId,
        bool isTyping)
    {
        var participant = EnsureParticipant(actorId);
        EnsureNotClosed("change typing state");

        _participants[actorId] = participant with { IsTyping = isTyping };
        var state = Snapshot();
        return new ConversationChange(
            state,
            [
                new ConversationEvent(
                    ConversationEventKind.TypingChanged,
                    state,
                    actorId,
                    participant.Role,
                    IsTyping: isTyping)
            ]);
    }

    public ConversationChange MarkIdle(long nowUnixMs)
    {
        if (Status != ConversationStatuses.Active || _idleDeadlineUnixMs is null || nowUnixMs < _idleDeadlineUnixMs)
            return new ConversationChange(Snapshot(), []);

        Status = ConversationStatuses.WaitingForClose;
        var state = Snapshot();
        return new ConversationChange(
            state,
            [new ConversationEvent(ConversationEventKind.Idle, state)]);
    }

    public ConversationChange Close(
        string actorId,
        string? reason)
    {
        _ = reason;
        EnsureParticipant(actorId);
        if (Status == ConversationStatuses.Closed)
            throw new InvalidOperationException("Conversation is already closed.");

        Status = ConversationStatuses.Closed;
        var state = Snapshot();
        return new ConversationChange(
            state,
            [new ConversationEvent(ConversationEventKind.Closed, state, actorId)]);
    }

    private ConversationParticipant EnsureParticipant(string actorId)
    {
        if (!_participants.TryGetValue(actorId, out var participant))
            throw new InvalidOperationException("Actor is not a conversation participant.");

        return participant;
    }

    private void EnsureNotClosed(string action)
    {
        if (Status == ConversationStatuses.Closed)
            throw new InvalidOperationException($"Closed conversation cannot {action}.");
    }

    private static ChatMessage ToContract(ConversationMessage message)
    {
        return new ChatMessage(
            message.ConversationId,
            message.MessageSeq,
            message.SenderActorId,
            message.Text,
            message.SentAtUnixMs);
    }
}