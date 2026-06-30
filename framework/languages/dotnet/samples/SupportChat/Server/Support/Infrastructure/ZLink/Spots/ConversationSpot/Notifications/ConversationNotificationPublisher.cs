using SupportChat.Server.Configuration;
using SupportChat.Server.Support.Domain.SupportChat;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Shared.Contracts;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot.Notifications;

internal sealed class ConversationNotificationPublisher
{
    public async ValueTask PublishAsync(
        IReadOnlyList<ConversationEvent> events,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        foreach (var conversationEvent in events) await PublishAsync(conversationEvent, actors, cancellationToken);
    }

    public ValueTask PublishJoinedAgentToCustomerAsync(
        SupportUserActor customer,
        ConversationState state,
        CancellationToken cancellationToken)
    {
        if (state.AgentActorId is null) return ValueTask.CompletedTask;

        customer.Context.BoundSession
            .Send(new ParticipantJoinedNotify(
                state.ConversationId,
                state.AgentActorId,
                SupportChatRoles.Agent,
                state))
            .PacketName(SampleNames.ParticipantJoinedPacket)
            .Submit(cancellationToken);
        return ValueTask.CompletedTask;
    }

    public ValueTask PublishAssignedToAgentAsync(
        SupportUserActor agent,
        ConversationState state,
        CancellationToken cancellationToken)
    {
        agent.Context.BoundSession
            .Send(new ConversationAssignedNotify(
                state.ConversationId,
                state))
            .PacketName(SampleNames.ConversationAssignedPacket)
            .Submit(cancellationToken);
        return ValueTask.CompletedTask;
    }

    private async ValueTask PublishAsync(
        ConversationEvent conversationEvent,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        switch (conversationEvent.Kind)
        {
            case ConversationEventKind.ParticipantJoined:
                await PublishParticipantJoinedAsync(conversationEvent, actors, cancellationToken);
                break;
            case ConversationEventKind.Assigned:
                await PublishAssignedAsync(conversationEvent, actors, cancellationToken);
                break;
            case ConversationEventKind.MessageAppended:
                await PublishMessageAsync(conversationEvent, actors, cancellationToken);
                break;
            case ConversationEventKind.TypingChanged:
                await PublishTypingAsync(conversationEvent, actors, cancellationToken);
                break;
            case ConversationEventKind.Idle:
                PublishAll(
                    actors,
                    actor => actor.Context.BoundSession
                        .Send(new ConversationIdleNotify(
                            conversationEvent.State.ConversationId,
                            conversationEvent.State))
                        .PacketName(SampleNames.ConversationIdlePacket)
                        .Submit(cancellationToken));
                break;
            case ConversationEventKind.Closed:
                PublishAll(
                    actors,
                    actor => actor.Context.BoundSession
                        .Send(new ConversationClosedNotify(
                            conversationEvent.State.ConversationId,
                            conversationEvent.State))
                        .PacketName(SampleNames.ConversationClosedPacket)
                        .Submit(cancellationToken));
                break;
            default:
                throw new InvalidOperationException($"Unsupported conversation event {conversationEvent.Kind}.");
        }
    }

    private static ValueTask PublishParticipantJoinedAsync(
        ConversationEvent conversationEvent,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        if (conversationEvent.ActorId is null || conversationEvent.Role is null)
            throw new InvalidOperationException("Participant joined event requires actor id and role.");

        var customerActorId = conversationEvent.State.CustomerActorId;
        if (actors.TryGetValue(customerActorId, out var customer)
            && !string.Equals(customer.ActorId, conversationEvent.ActorId, StringComparison.Ordinal))
            customer.Context.BoundSession
                .Send(new ParticipantJoinedNotify(
                    conversationEvent.State.ConversationId,
                    conversationEvent.ActorId,
                    conversationEvent.Role,
                    conversationEvent.State))
                .PacketName(SampleNames.ParticipantJoinedPacket)
                .Submit(cancellationToken);
        return ValueTask.CompletedTask;
    }

    private static ValueTask PublishAssignedAsync(
        ConversationEvent conversationEvent,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        if (conversationEvent.ActorId is null || !actors.TryGetValue(conversationEvent.ActorId, out var agent))
            return ValueTask.CompletedTask;

        agent.Context.BoundSession
            .Send(new ConversationAssignedNotify(
                conversationEvent.State.ConversationId,
                conversationEvent.State))
            .PacketName(SampleNames.ConversationAssignedPacket)
            .Submit(cancellationToken);
        return ValueTask.CompletedTask;
    }

    private static ValueTask PublishMessageAsync(
        ConversationEvent conversationEvent,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        var message = conversationEvent.Message
                      ?? throw new InvalidOperationException("Message event requires a chat message.");
        PublishAll(
            actors.Where(actor => !string.Equals(actor.Key, message.SenderActorId, StringComparison.Ordinal))
                .ToDictionary(static actor => actor.Key, static actor => actor.Value, StringComparer.Ordinal),
            actor => actor.Context.BoundSession
                .Send(new ChatMessageNotify(
                    conversationEvent.State.ConversationId,
                    message,
                    conversationEvent.State))
                .PacketName(SampleNames.ChatMessagePacket)
                .Submit(cancellationToken));
        return ValueTask.CompletedTask;
    }

    private static ValueTask PublishTypingAsync(
        ConversationEvent conversationEvent,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        if (conversationEvent.ActorId is null || conversationEvent.IsTyping is null)
            throw new InvalidOperationException("Typing event requires actor id and typing state.");

        PublishAll(
            actors.Where(actor => !string.Equals(actor.Key, conversationEvent.ActorId, StringComparison.Ordinal))
                .ToDictionary(static actor => actor.Key, static actor => actor.Value, StringComparer.Ordinal),
            actor => actor.Context.BoundSession
                .Send(new TypingChangedNotify(
                    conversationEvent.State.ConversationId,
                    conversationEvent.ActorId,
                    conversationEvent.IsTyping.Value,
                    conversationEvent.State))
                .PacketName(SampleNames.TypingChangedPacket)
                .Submit(cancellationToken));
        return ValueTask.CompletedTask;
    }

    private static void PublishAll(
        IReadOnlyDictionary<string, SupportUserActor> actors,
        Action<SupportUserActor> publish)
    {
        foreach (var actor in actors.Values) publish(actor);
    }
}
