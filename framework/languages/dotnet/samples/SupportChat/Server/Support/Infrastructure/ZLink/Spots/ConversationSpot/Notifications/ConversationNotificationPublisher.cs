using SupportChat.Server.Configuration;
using SupportChat.Server.Support.Domain.SupportChat;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Shared.Contracts;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot.Notifications;

// Adapter: turns pure domain conversation events into stream push messages and sends
// them to the participants' bound sessions (§8). Domain snapshots/messages are mapped
// to wire contracts through ConversationContracts.
internal sealed class ConversationNotificationPublisher
{
    public async ValueTask PublishAsync(
        IReadOnlyList<ConversationEvent> events,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        foreach (var conversationEvent in events) await PublishAsync(conversationEvent, actors, cancellationToken);
    }

    // Sent when a conversation is assigned to an agent, before the agent joins. It goes
    // to the agent's roster actor so the agent client knows which conversation to join.
    public ValueTask PublishAssignedToRosterAsync(
        SupportUserActor roster,
        ConversationSnapshot snapshot,
        CancellationToken cancellationToken)
    {
        var state = ConversationContracts.ToState(snapshot);
        roster.Context.BoundSession
            .Send(new ConversationAssignedNotify(state.ConversationId, state))
            .Submit(cancellationToken);
        return ValueTask.CompletedTask;
    }

    private async ValueTask PublishAsync(
        ConversationEvent conversationEvent,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        var state = ConversationContracts.ToState(conversationEvent.State);
        switch (conversationEvent.Kind)
        {
            case ConversationEventKind.ParticipantJoined:
                await PublishParticipantJoinedAsync(conversationEvent, state, actors, cancellationToken);
                break;
            case ConversationEventKind.MessageAppended:
                PublishMessageAsync(conversationEvent, state, actors, cancellationToken);
                break;
            case ConversationEventKind.TypingChanged:
                PublishTypingAsync(conversationEvent, state, actors, cancellationToken);
                break;
            case ConversationEventKind.Idle:
                PublishAll(
                    actors,
                    actor => actor.Context.BoundSession
                        .Send(new ConversationIdleNotify(state.ConversationId, state))
                        .Submit(cancellationToken));
                break;
            case ConversationEventKind.Closed:
                // An explicit close carries the requester's participant id: that client
                // already gets the closed state in CloseConversationRes, so only the
                // other participant is notified. An auto-close (idle grace) has no
                // requester, so both participants are notified.
                PublishAll(
                    conversationEvent.ActorId is null
                        ? actors
                        : Exclude(actors, conversationEvent.ActorId),
                    actor => actor.Context.BoundSession
                        .Send(new ConversationClosedNotify(state.ConversationId, state))
                        .Submit(cancellationToken));
                break;
            default:
                throw new InvalidOperationException($"Unsupported conversation event {conversationEvent.Kind}.");
        }
    }

    private static ValueTask PublishParticipantJoinedAsync(
        ConversationEvent conversationEvent,
        ConversationState state,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        if (conversationEvent.ActorId is null || conversationEvent.Role is null)
            throw new InvalidOperationException("Participant joined event requires actor id and role.");

        // Tell the customer that the agent joined (the joining participant is excluded).
        if (actors.TryGetValue(state.CustomerActorId, out var customer)
            && !string.Equals(customer.ParticipantId, conversationEvent.ActorId, StringComparison.Ordinal))
            customer.Context.BoundSession
                .Send(new ParticipantJoinedNotify(
                    state.ConversationId,
                    conversationEvent.ActorId,
                    ConversationContracts.ToRole(conversationEvent.Role.Value),
                    state))
                .Submit(cancellationToken);
        return ValueTask.CompletedTask;
    }

    private static void PublishMessageAsync(
        ConversationEvent conversationEvent,
        ConversationState state,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        var message = conversationEvent.Message
                      ?? throw new InvalidOperationException("Message event requires a chat message.");
        var chatMessage = ConversationContracts.ToMessage(message);
        PublishAll(
            Exclude(actors, message.SenderActorId),
            actor => actor.Context.BoundSession
                .Send(new ChatMessageNotify(state.ConversationId, chatMessage, state))
                .Submit(cancellationToken));
    }

    private static void PublishTypingAsync(
        ConversationEvent conversationEvent,
        ConversationState state,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        if (conversationEvent.ActorId is null || conversationEvent.IsTyping is null)
            throw new InvalidOperationException("Typing event requires actor id and typing state.");

        PublishAll(
            Exclude(actors, conversationEvent.ActorId),
            actor => actor.Context.BoundSession
                .Send(new TypingChangedNotify(
                    state.ConversationId,
                    conversationEvent.ActorId,
                    conversationEvent.IsTyping.Value,
                    state))
                .Submit(cancellationToken));
    }

    private static IReadOnlyDictionary<string, SupportUserActor> Exclude(
        IReadOnlyDictionary<string, SupportUserActor> actors,
        string participantId)
    {
        return actors
            .Where(actor => !string.Equals(actor.Key, participantId, StringComparison.Ordinal))
            .ToDictionary(static actor => actor.Key, static actor => actor.Value, StringComparer.Ordinal);
    }

    private static void PublishAll(
        IReadOnlyDictionary<string, SupportUserActor> actors,
        Action<SupportUserActor> publish)
    {
        foreach (var actor in actors.Values) publish(actor);
    }
}
