using GameQuest.Server.Configuration;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace GameQuest.GameApi.Session;

internal sealed class PlayerSessionActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;

    public void EnsurePlayer(string playerId)
    {
        if (!string.Equals(ActorId, playerId, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"Bound player '{ActorId}' cannot act for player '{playerId}'.");
    }
}

internal sealed class PlayerSessionActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult<IZLinkActor>(new PlayerSessionActor(actorId, context));
}

internal sealed class GameQuestEntrySpot(
    IZLinkEntrySpotContext context,
    ILogger<GameQuestEntrySpot> logger) : IZLinkEntrySpot<PlayerSessionActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public ValueTask OnCreateActorAsync(
        PlayerSessionActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        logger.LogInformation("gamequest session actor created player={PlayerId}", actor.ActorId);
        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken) =>
        ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());

    public ValueTask OnJoinedActorAsync(PlayerSessionActor actor, CancellationToken cancellationToken) =>
        ValueTask.CompletedTask;

    public ValueTask OnLeaveActorAsync(PlayerSessionActor actor, CancellationToken cancellationToken) =>
        ValueTask.CompletedTask;

    public ValueTask OnDisconnectActorAsync(PlayerSessionActor actor, CancellationToken cancellationToken)
    {
        logger.LogInformation("gamequest session actor disconnected player={PlayerId}", actor.ActorId);
        return ValueTask.CompletedTask;
    }
}

[ZLinkSpotActorSendHandler(nameof(QuestProgressNotify))]
internal sealed class QuestProgressNotifyActorHandler
    : IZLinkEntrySpotActorSendHandler<
        GameQuestEntrySpot,
        PlayerSessionActor,
        QuestProgressNotify>
{
    public async ValueTask HandleAsync(
        GameQuestEntrySpot entrySpot,
        PlayerSessionActor actor,
        ZLinkSpotActorSendContext context,
        QuestProgressNotify request,
        CancellationToken cancellationToken)
    {
        await actor.Context.BoundSession.Send(request).Async(cancellationToken);
    }
}

[ZLinkSpotActorSendHandler(nameof(QuestCompletedNotify))]
internal sealed class QuestCompletedNotifyActorHandler
    : IZLinkEntrySpotActorSendHandler<
        GameQuestEntrySpot,
        PlayerSessionActor,
        QuestCompletedNotify>
{
    public async ValueTask HandleAsync(
        GameQuestEntrySpot entrySpot,
        PlayerSessionActor actor,
        ZLinkSpotActorSendContext context,
        QuestCompletedNotify request,
        CancellationToken cancellationToken)
    {
        await actor.Context.BoundSession.Send(request).Async(cancellationToken);
    }
}
