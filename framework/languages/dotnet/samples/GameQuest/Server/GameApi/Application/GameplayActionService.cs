using GameQuest.GameApi.Domain;
using GameQuest.Server.Configuration;
using GameQuest.Shared;

namespace GameQuest.GameApi.Application;

internal sealed class GameplayActionService(
    IGameplayEventStore store,
    IGameplayEventOwnerDispatcher ownerDispatcher,
    IQuestProgressSynchronizer quests,
    GameQuestRuntimeConfiguration configuration,
    ILogger<GameplayActionService> logger)
{
    private readonly string _apiName = configuration.InstanceName;

    public async ValueTask<KillMonsterRes> KillMonsterAsync(
        KillMonsterReq request,
        CancellationToken cancellationToken)
    {
        var dispatched = await StoreAndDispatchAsync(
            GameplayDomain.CreateMonsterKilled(request.PlayerId, request.MonsterId, request.AreaId,
                request.IdempotencyKey, _apiName),
            cancellationToken);
        return new KillMonsterRes(dispatched.EventId);
    }

    public async ValueTask<CollectItemRes> CollectItemAsync(
        CollectItemReq request,
        CancellationToken cancellationToken)
    {
        var dispatched = await StoreAndDispatchAsync(
            GameplayDomain.CreateItemCollected(request.PlayerId, request.ItemId, request.Count, request.IdempotencyKey,
                _apiName),
            cancellationToken);
        return new CollectItemRes(dispatched.EventId);
    }

    public async ValueTask<CompleteMissionRes> CompleteMissionAsync(
        CompleteMissionReq request,
        CancellationToken cancellationToken)
    {
        var dispatched = await StoreAndDispatchAsync(
            GameplayDomain.CreateMissionCompleted(request.PlayerId, request.MissionId, request.IdempotencyKey,
                _apiName),
            cancellationToken);
        return new CompleteMissionRes(dispatched.EventId);
    }

    public async ValueTask<EnterAreaRes> EnterAreaAsync(
        EnterAreaReq request,
        CancellationToken cancellationToken)
    {
        var dispatched = await StoreAndDispatchAsync(
            GameplayDomain.CreateAreaEntered(request.PlayerId, request.AreaId, request.IdempotencyKey, _apiName),
            cancellationToken);
        return new EnterAreaRes(dispatched.EventId);
    }

    public async ValueTask<UnlockFeatureRes> UnlockFeatureAsync(
        UnlockFeatureReq request,
        CancellationToken cancellationToken)
    {
        var dispatched = await StoreAndDispatchAsync(
            GameplayDomain.CreateFeatureUnlocked(request.PlayerId, request.FeatureId, request.IdempotencyKey, _apiName),
            cancellationToken);
        return new UnlockFeatureRes(dispatched.EventId);
    }

    public async ValueTask<SyncQuestProgressRes> SyncAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        return await quests.SyncAsync(playerId, cancellationToken);
    }

    private async ValueTask<GameplayEventEnvelope> StoreAndDispatchAsync(
        GameplayEventEnvelope candidate,
        CancellationToken cancellationToken)
    {
        var stored = await store.GetOrAddGameplayEventAsync(candidate, cancellationToken);
        var routedTo = await ownerDispatcher.DispatchAsync(stored, cancellationToken);
        logger.LogInformation(
            "gamequest api event routed api={Api} player={PlayerId} event={EventId} type={EventType} owner={Owner}",
            _apiName,
            stored.PlayerId,
            stored.EventId,
            stored.EventType,
            routedTo);
        return stored;
    }
}

internal sealed class JoinQuestSessionUseCase(IQuestSessionStore sessions)
{
    public async ValueTask<JoinSessionRes> ExecuteAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        var projection = await sessions.ReadProjectionAsync(playerId, cancellationToken);
        return new JoinSessionRes(projection);
    }
}

internal interface IGameplayEventStore
{
    ValueTask<GameplayEventEnvelope> GetOrAddGameplayEventAsync(
        GameplayEventEnvelope candidate,
        CancellationToken cancellationToken);
}

internal interface IGameplayEventOwnerDispatcher
{
    ValueTask<string> DispatchAsync(
        GameplayEventEnvelope gameplayEvent,
        CancellationToken cancellationToken);
}

internal interface IQuestProgressSynchronizer
{
    ValueTask<SyncQuestProgressRes> SyncAsync(
        string playerId,
        CancellationToken cancellationToken);
}

internal interface IQuestSessionStore
{
    ValueTask<QuestProgress[]> ReadProjectionAsync(
        string playerId,
        CancellationToken cancellationToken);
}
