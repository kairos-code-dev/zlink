using GameQuest.GameApi.Domain;
using GameQuest.Shared;

namespace GameQuest.GameApi.Application;

internal sealed class GameplayActionService(
    IGameplayEventStore store,
    IGameplayEventPublisher publisher,
    IQuestProgressSynchronizer quests,
    ILogger<GameplayActionService> logger)
{
    private readonly string _apiName = Environment.GetEnvironmentVariable("GAMEQUEST_API_NAME") ?? "api";

    public async ValueTask<KillMonsterRes> KillMonsterAsync(
        KillMonsterReq request,
        CancellationToken cancellationToken)
    {
        var published = await StoreAndPublishAsync(
            GameplayDomain.CreateMonsterKilled(request.PlayerId, request.MonsterId, request.AreaId,
                request.IdempotencyKey, _apiName),
            cancellationToken);
        return new KillMonsterRes(published.EventId);
    }

    public async ValueTask<CollectItemRes> CollectItemAsync(
        CollectItemReq request,
        CancellationToken cancellationToken)
    {
        var published = await StoreAndPublishAsync(
            GameplayDomain.CreateItemCollected(request.PlayerId, request.ItemId, request.Count, request.IdempotencyKey,
                _apiName),
            cancellationToken);
        return new CollectItemRes(published.EventId);
    }

    public async ValueTask<CompleteMissionRes> CompleteMissionAsync(
        CompleteMissionReq request,
        CancellationToken cancellationToken)
    {
        var published = await StoreAndPublishAsync(
            GameplayDomain.CreateMissionCompleted(request.PlayerId, request.MissionId, request.IdempotencyKey,
                _apiName),
            cancellationToken);
        return new CompleteMissionRes(published.EventId);
    }

    public async ValueTask<EnterAreaRes> EnterAreaAsync(
        EnterAreaReq request,
        CancellationToken cancellationToken)
    {
        var published = await StoreAndPublishAsync(
            GameplayDomain.CreateAreaEntered(request.PlayerId, request.AreaId, request.IdempotencyKey, _apiName),
            cancellationToken);
        return new EnterAreaRes(published.EventId);
    }

    public async ValueTask<UnlockFeatureRes> UnlockFeatureAsync(
        UnlockFeatureReq request,
        CancellationToken cancellationToken)
    {
        var published = await StoreAndPublishAsync(
            GameplayDomain.CreateFeatureUnlocked(request.PlayerId, request.FeatureId, request.IdempotencyKey, _apiName),
            cancellationToken);
        return new UnlockFeatureRes(published.EventId);
    }

    public async ValueTask<SyncQuestProgressRes> SyncAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        return await quests.SyncAsync(playerId, cancellationToken);
    }

    private async ValueTask<GameplayEventEnvelope> StoreAndPublishAsync(
        GameplayEventEnvelope candidate,
        CancellationToken cancellationToken)
    {
        var stored = await store.GetOrAddGameplayEventAsync(candidate, cancellationToken);
        var publishedFrom = await publisher.PublishAsync(stored, cancellationToken);
        logger.LogInformation(
            "gamequest api event published api={Api} player={PlayerId} event={EventId} type={EventType} fanout={Fanout}",
            _apiName,
            stored.PlayerId,
            stored.EventId,
            stored.EventType,
            publishedFrom);
        return stored;
    }
}

internal interface IGameplayEventStore
{
    ValueTask<GameplayEventEnvelope> GetOrAddGameplayEventAsync(
        GameplayEventEnvelope candidate,
        CancellationToken cancellationToken);
}

internal interface IGameplayEventPublisher
{
    ValueTask<string> PublishAsync(
        GameplayEventEnvelope gameplayEvent,
        CancellationToken cancellationToken);
}

internal interface IQuestProgressSynchronizer
{
    ValueTask<SyncQuestProgressRes> SyncAsync(
        string playerId,
        CancellationToken cancellationToken);
}