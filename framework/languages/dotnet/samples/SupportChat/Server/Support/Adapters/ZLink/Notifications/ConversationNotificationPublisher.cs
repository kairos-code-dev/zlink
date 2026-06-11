using SupportChat.Server.Support.Adapters.ZLink.Actors;
using SupportChat.Server.Support.Domain.SupportChat;
using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;

namespace SupportChat.Server.Support.Adapters.ZLink.Notifications;

internal sealed class ConversationNotificationPublisher
{
    public async ValueTask PublishAsync(
        IReadOnlyList<ConversationEvent> events,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        foreach (var conversationEvent in events)
        {
            await PublishAsync(conversationEvent, actors, cancellationToken);
        }
    }

    public async ValueTask PublishJoinedAgentToCustomerAsync(
        SupportUserActor customer,
        ConversationState state,
        CancellationToken cancellationToken)
    {
        if (state.AgentActorId is null)
        {
            return;
        }

        await customer.Context.BoundSession
            .Send(new ParticipantJoinedNotify(
                state.ConversationId,
                state.AgentActorId,
                SupportChatRoles.Agent,
                state))
            .PacketName(SampleNames.ParticipantJoinedPacket)
            .Async(cancellationToken);
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
                await PublishAllAsync(
                    actors,
                    actor => actor.Context.BoundSession
                        .Send(new ConversationIdleNotify(
                            conversationEvent.State.ConversationId,
                            conversationEvent.State))
                        .PacketName(SampleNames.ConversationIdlePacket)
                        .Async(cancellationToken));
                break;
            case ConversationEventKind.Closed:
                await PublishAllAsync(
                    actors,
                    actor => actor.Context.BoundSession
                        .Send(new ConversationClosedNotify(
                            conversationEvent.State.ConversationId,
                            conversationEvent.State))
                        .PacketName(SampleNames.ConversationClosedPacket)
                        .Async(cancellationToken));
                break;
            default:
                throw new InvalidOperationException($"Unsupported conversation event {conversationEvent.Kind}.");
        }
    }

    private static async ValueTask PublishParticipantJoinedAsync(
        ConversationEvent conversationEvent,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        if (conversationEvent.ActorId is null || conversationEvent.Role is null)
        {
            throw new InvalidOperationException("Participant joined event requires actor id and role.");
        }

        var customerActorId = conversationEvent.State.CustomerActorId;
        if (actors.TryGetValue(customerActorId, out var customer)
            && !string.Equals(customer.ActorId, conversationEvent.ActorId, StringComparison.Ordinal))
        {
            await customer.Context.BoundSession
                .Send(new ParticipantJoinedNotify(
                    conversationEvent.State.ConversationId,
                    conversationEvent.ActorId,
                    conversationEvent.Role,
                    conversationEvent.State))
                .PacketName(SampleNames.ParticipantJoinedPacket)
                .Async(cancellationToken);
        }
    }

    private static async ValueTask PublishAssignedAsync(
        ConversationEvent conversationEvent,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        if (conversationEvent.ActorId is null || !actors.TryGetValue(conversationEvent.ActorId, out var agent))
        {
            return;
        }

        await agent.Context.BoundSession
            .Send(new ConversationAssignedNotify(
                conversationEvent.State.ConversationId,
                conversationEvent.State))
            .PacketName(SampleNames.ConversationAssignedPacket)
            .Async(cancellationToken);
    }

    private static async ValueTask PublishMessageAsync(
        ConversationEvent conversationEvent,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        var message = conversationEvent.Message
            ?? throw new InvalidOperationException("Message event requires a chat message.");
        await PublishAllAsync(
            actors.Where(actor => !string.Equals(actor.Key, message.SenderActorId, StringComparison.Ordinal))
                .ToDictionary(static actor => actor.Key, static actor => actor.Value, StringComparer.Ordinal),
            actor => actor.Context.BoundSession
                .Send(new ChatMessageNotify(
                    conversationEvent.State.ConversationId,
                    message,
                    conversationEvent.State))
                .PacketName(SampleNames.ChatMessagePacket)
                .Async(cancellationToken));
    }

    private static async ValueTask PublishTypingAsync(
        ConversationEvent conversationEvent,
        IReadOnlyDictionary<string, SupportUserActor> actors,
        CancellationToken cancellationToken)
    {
        if (conversationEvent.ActorId is null || conversationEvent.IsTyping is null)
        {
            throw new InvalidOperationException("Typing event requires actor id and typing state.");
        }

        await PublishAllAsync(
            actors.Where(actor => !string.Equals(actor.Key, conversationEvent.ActorId, StringComparison.Ordinal))
                .ToDictionary(static actor => actor.Key, static actor => actor.Value, StringComparer.Ordinal),
            actor => actor.Context.BoundSession
                .Send(new TypingChangedNotify(
                    conversationEvent.State.ConversationId,
                    conversationEvent.ActorId,
                    conversationEvent.IsTyping.Value,
                    conversationEvent.State))
                .PacketName(SampleNames.TypingChangedPacket)
                .Async(cancellationToken));
    }

    private static async ValueTask PublishAllAsync(
        IReadOnlyDictionary<string, SupportUserActor> actors,
        Func<SupportUserActor, ValueTask> publish)
    {
        foreach (var actor in actors.Values)
        {
            await publish(actor);
        }
    }
}
