using Microsoft.Extensions.Logging;
using SupportChat.Server.Configuration;
using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Server.Support.Domain.SupportChat;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot.Handlers;
using SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot.Notifications;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot;

internal sealed class ConversationSpot(
    IZLinkSpotContext context,
    ConversationNotificationPublisher notifications,
    AgentAssignmentService assignment,
    SupportActorDirectory directory,
    ILogger<ConversationSpot> logger) : IZLinkSpot<SupportUserActor>
{
    internal static readonly TimeSpan IdleCheckPeriod = TimeSpan.FromMilliseconds(200);

    // Push recipients keyed by participant id (customer id or agent roster id). The
    // value is the actor whose bound session receives pushes. For the customer that is
    // its own actor; for an agent it is the single roster actor, because an agent
    // session's per-conversation actors are used only for inbound handling — outbound
    // pushes to a session's non-first actor are not reliably delivered, so all of an
    // agent's rooms fan out through its one roster actor (disambiguated by ConversationId).
    private readonly Dictionary<string, SupportUserActor> _actors = new(StringComparer.Ordinal);
    private Conversation? _conversation;

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<JoinConversationHandler>();
        Context.Handlers.AddHandler<SendChatMessageHandler>();
        Context.Handlers.AddActorSend<SetTypingHandler, SupportUserActor>(nameof(SetTypingReq));
        Context.Handlers.AddHandler<CloseConversationHandler>();
    }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var create = request.Decode<ConversationCreateReq>();
        var conversationId = Context.SpotRid.ToString();
        _conversation = new Conversation(
            conversationId,
            create.Subject,
            create.CustomerActorId,
            create.CustomerDisplayName,
            create.CreatedAtUnixMs,
            new ConversationPolicy(
                SampleTimings.IdleTimeout,
                SampleTimings.CloseGraceTimeout,
                500));
        logger.LogInformation(
            "support conversation: created. conversation={ConversationId}, customer={CustomerActorId}",
            conversationId,
            create.CustomerActorId);
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    public async ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        await ValueTask.CompletedTask;
    }

    // Timer tick (registered by ConversationIdleTimerHandler): advance the idle
    // lifecycle one stage. No-op until the conversation exists and has messages.
    public ValueTask CheckIdleAsync(CancellationToken cancellationToken)
    {
        if (_conversation is null) return ValueTask.CompletedTask;
        var change = _conversation.MarkIdle(NowUnixMs());
        return PublishChangeAsync(change, cancellationToken);
    }

    // The conversation is identified by the spot routing id, so an actor only reaches
    // this spot when it is joining this conversation. The customer's own actor is the
    // participant; each agent joins through its own per-conversation actor.
    public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        SupportUserActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var conversation = RequireConversation();

        if (string.Equals(actor.Role, SupportChatRoles.Agent, StringComparison.Ordinal))
        {
            var change = JoinAgent(actor);
            await PublishChangeAsync(change, cancellationToken);
            return ZLinkSpotActorJoinResult.Accept(new JoinConversationRes(ConversationContracts.ToState(change.State)));
        }

        actor.JoinConversation(conversation.ConversationId);
        _actors[actor.ParticipantId] = actor;

        // The customer has joined; pick a capacity-available agent and notify its roster.
        await AssignAgentAsync(cancellationToken);

        logger.LogInformation(
            "support conversation: actor joined. conversation={ConversationId}, participant={ParticipantId}, role={Role}",
            conversation.ConversationId,
            actor.ParticipantId,
            actor.Role);
        return ZLinkSpotActorJoinResult.Accept(new JoinConversationRes(ConversationContracts.ToState(conversation.Snapshot())));
    }

    // Reserves a capacity-available agent for this conversation and notifies its roster
    // actor. Availability is reserved here and released on close (see PublishChangeAsync).
    private async ValueTask AssignAgentAsync(CancellationToken cancellationToken)
    {
        var conversation = RequireConversation();
        var assigned = assignment.AssignForConversation(conversation.ConversationId);
        if (assigned is null) return;

        await notifications.PublishAssignedToRosterAsync(
            directory.Get(assigned.RosterActorId).Actor,
            conversation.Snapshot(),
            cancellationToken);
        logger.LogInformation(
            "support conversation: assigned. conversation={ConversationId}, roster={RosterActorId}",
            conversation.ConversationId,
            assigned.RosterActorId);
    }

    // A reconnected client has a fresh session and no local conversation view, so it
    // re-fetches the current state through JoinConversationReq. Membership already
    // follows the actor, so this only refreshes the bound actor entry and returns the
    // latest snapshot.
    public JoinConversationRes RefreshMembership(SupportUserActor actor)
    {
        var conversation = RequireConversation();
        _actors[actor.ParticipantId] = actor;
        logger.LogInformation(
            "support conversation: membership refreshed. conversation={ConversationId}, participant={ParticipantId}",
            conversation.ConversationId,
            actor.ParticipantId);
        return new JoinConversationRes(ConversationContracts.ToState(conversation.Snapshot()));
    }

    public async ValueTask<SendChatMessageRes> SendMessageAsync(
        SupportUserActor actor,
        SendChatMessageReq request,
        CancellationToken cancellationToken)
    {
        var change = RequireConversation().SendMessage(actor.ParticipantId, request.Text, NowUnixMs());
        await PublishChangeAsync(change, cancellationToken);
        var appended = change.Events.Single(static item => item.Kind == ConversationEventKind.MessageAppended).Message
                       ?? throw new InvalidOperationException("Message event was not created.");
        return new SendChatMessageRes(
            ConversationContracts.ToMessage(appended),
            ConversationContracts.ToState(change.State));
    }

    public async ValueTask SetTypingAsync(
        SupportUserActor actor,
        SetTypingReq request,
        CancellationToken cancellationToken)
    {
        var change = RequireConversation().SetTyping(actor.ParticipantId, request.IsTyping);
        await PublishChangeAsync(change, cancellationToken);
    }

    public async ValueTask<CloseConversationRes> CloseAsync(
        SupportUserActor actor,
        CloseConversationReq request,
        CancellationToken cancellationToken)
    {
        var change = RequireConversation().Close(actor.ParticipantId, request.Reason);
        await PublishChangeAsync(change, cancellationToken);
        return new CloseConversationRes(ConversationContracts.ToState(change.State));
    }

    private ConversationChange JoinAgent(SupportUserActor agent)
    {
        var conversation = RequireConversation();
        var change = conversation.JoinAgent(agent.ParticipantId, agent.DisplayName, NowUnixMs());
        agent.JoinConversation(conversation.ConversationId);
        // Fan out this room's pushes through the agent's single roster actor.
        _actors[agent.ParticipantId] = directory.Get(agent.ParticipantId).Actor;
        return change;
    }

    // Publishes conversation events and, when the conversation closes, frees the
    // assigned agent's capacity so it can take new conversations.
    private async ValueTask PublishChangeAsync(ConversationChange change, CancellationToken cancellationToken)
    {
        await notifications.PublishAsync(change.Events, _actors, cancellationToken);
        if (change.Events.Any(static item => item.Kind == ConversationEventKind.Closed))
            assignment.ReleaseConversation(RequireConversation().ConversationId);
    }

    private Conversation RequireConversation()
    {
        return _conversation ?? throw new InvalidOperationException("Conversation has not been created.");
    }

    private static long NowUnixMs()
    {
        return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
    }
}
