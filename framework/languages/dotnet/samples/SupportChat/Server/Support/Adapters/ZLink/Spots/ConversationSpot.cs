using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using SupportChat.Server.Support.Adapters.ZLink.Actors;
using SupportChat.Server.Support.Adapters.ZLink.Notifications;
using SupportChat.Server.Support.Adapters.ZLink.Spots.Handlers;
using SupportChat.Server.Support.Domain.SupportChat;
using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Adapters.ZLink.Spots;

internal sealed class ConversationSpot(
    IZLinkSpotContext context,
    ConversationNotificationPublisher notifications,
    ILogger<ConversationSpot> logger) : IZLinkSpot<SupportUserActor>
{
    internal static readonly TimeSpan IdleCheckPeriod = TimeSpan.FromMilliseconds(200);

    private readonly Dictionary<string, SupportUserActor> _actors = new(StringComparer.Ordinal);
    private Conversation? _conversation;

    public IZLinkSpotContext Context { get; } = context;

    public void Configure()
    {
        Context.Handlers.AddHandler<SendChatMessageHandler>();
        Context.Handlers.AddHandler<SetTypingHandler>();
        Context.Handlers.AddHandler<CloseConversationHandler>();
    }

    public async ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        Message request,
        CancellationToken cancellationToken)
    {
        var create = request.FromJson<ConversationCreateRequest>();
        var conversationId = Context.SpotRid.ToHex();
        _conversation = new Conversation(
            conversationId,
            create.Subject,
            create.CustomerActorId,
            create.CustomerDisplayName,
            create.CreatedAtUnixMs,
            new ConversationPolicy(
                SampleTimings.IdleTimeout,
                SampleTimings.CloseGraceTimeout,
                MaxMessageLength: 500));
        _ = cancellationToken;
        logger.LogInformation(
            "support conversation: created. conversation={ConversationId}, customer={CustomerActorId}",
            conversationId,
            create.CustomerActorId);
        Console.Error.WriteLine($"support conversation: created conversation={conversationId} customer={create.CustomerActorId}");
        return ZLinkSpotCreateResponse.Accept();
    }

    public async ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        await ValueTask.CompletedTask;
    }

    public async ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        SupportUserActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        var join = request.Decode<JoinConversationReq>();
        var conversation = RequireConversation();
        if (!string.Equals(join.ConversationId, conversation.ConversationId, StringComparison.Ordinal))
        {
            return ZLinkSpotActorJoinResult.Reject();
        }

        if (string.Equals(actor.Role, SupportChatRoles.Agent, StringComparison.Ordinal))
        {
            var agentState = await JoinAgentAsync(actor, cancellationToken);
            return ZLinkSpotActorJoinResult.Accept(new JoinConversationRes(agentState).Encode());
        }

        actor.JoinConversation(join.ConversationId);
        _actors[actor.ActorId] = actor;
        if (string.Equals(actor.ActorId, conversation.CustomerActorId, StringComparison.Ordinal))
        {
            await notifications.PublishJoinedAgentToCustomerAsync(
                actor,
                conversation.Snapshot(),
                cancellationToken);
        }

        logger.LogInformation(
            "support conversation: actor joined. conversation={ConversationId}, actor={ActorId}, role={Role}",
            join.ConversationId,
            actor.ActorId,
            actor.Role);
        Console.Error.WriteLine($"support conversation: actor joined conversation={join.ConversationId} actor={actor.ActorId} role={actor.Role}");
        return ZLinkSpotActorJoinResult.Accept(new JoinConversationRes(conversation.Snapshot()).Encode());
    }

    public async ValueTask<ConversationState> JoinAgentAsync(
        SupportUserActor agent,
        CancellationToken cancellationToken)
    {
        var conversation = RequireConversation();
        var change = conversation.JoinAgent(agent.ActorId, agent.DisplayName, NowUnixMs());
        agent.JoinConversation(conversation.ConversationId);
        _actors[agent.ActorId] = agent;
        await notifications.PublishAsync(change.Events, _actors, cancellationToken);
        return change.State;
    }

    public async ValueTask<SendChatMessageRes> SendMessageAsync(
        SupportUserActor actor,
        SendChatMessageReq request,
        CancellationToken cancellationToken)
    {
        EnsureConversationId(request.ConversationId);
        var change = RequireConversation().SendMessage(actor.ActorId, request.Text, NowUnixMs());
        await notifications.PublishAsync(change.Events, _actors, cancellationToken);
        ScheduleIdleCheck();
        return new SendChatMessageRes(
            change.Events.Single(static item => item.Kind == ConversationEventKind.MessageAppended).Message
                ?? throw new InvalidOperationException("Message event was not created."),
            change.State);
    }

    public async ValueTask<SetTypingRes> SetTypingAsync(
        SupportUserActor actor,
        SetTypingReq request,
        CancellationToken cancellationToken)
    {
        EnsureConversationId(request.ConversationId);
        var change = RequireConversation().SetTyping(actor.ActorId, request.IsTyping);
        await notifications.PublishAsync(change.Events, _actors, cancellationToken);
        return new SetTypingRes(change.State);
    }

    public async ValueTask<CloseConversationRes> CloseAsync(
        SupportUserActor actor,
        CloseConversationReq request,
        CancellationToken cancellationToken)
    {
        EnsureConversationId(request.ConversationId);
        var change = RequireConversation().Close(actor.ActorId, request.Reason);
        await notifications.PublishAsync(change.Events, _actors, cancellationToken);
        return new CloseConversationRes(change.State);
    }

    internal async ValueTask CheckIdleAsync(CancellationToken cancellationToken)
    {
        var change = RequireConversation().MarkIdle(NowUnixMs());
        await notifications.PublishAsync(change.Events, _actors, cancellationToken);
    }

    private void EnsureConversationId(string conversationId)
    {
        var conversation = RequireConversation();
        if (!string.Equals(conversationId, conversation.ConversationId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Request targets another conversation. conversation={conversationId}");
        }
    }

    private Conversation RequireConversation()
    {
        return _conversation ?? throw new InvalidOperationException("Conversation has not been created.");
    }

    private static long NowUnixMs()
    {
        return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
    }

    private void ScheduleIdleCheck()
    {
        _ = Task.Run(async () =>
        {
            await Task.Delay(SampleTimings.IdleTimeout);
            while (true)
            {
                var conversation = RequireConversation();
                var change = conversation.MarkIdle(NowUnixMs());
                await notifications.PublishAsync(change.Events, _actors, CancellationToken.None);
                if (change.Events.Count > 0
                    || !string.Equals(change.State.Status, ConversationStatuses.Active, StringComparison.Ordinal)
                    || change.State.IdleDeadlineUnixMs is null)
                {
                    return;
                }

                await Task.Delay(IdleCheckPeriod);
            }
        });
    }
}
