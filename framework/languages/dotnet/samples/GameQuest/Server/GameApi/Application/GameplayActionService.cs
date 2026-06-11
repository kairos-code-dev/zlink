using GameQuest.GameApi.Adapters.Store;
using GameQuest.GameApi.Domain;
using GameQuest.Shared;
using Zlink.Framework.Contracts.Channels;

namespace GameQuest.GameApi.Application;

internal sealed class GameplayActionService(
    GameQuestStore store,
    IZLinkFanoutClient fanout,
    GameQuestTopology topology,
    IHttpClientFactory httpClientFactory,
    ILogger<GameplayActionService> logger)
{
    private readonly string _apiName = Environment.GetEnvironmentVariable("GAMEQUEST_API_NAME") ?? "api";

    public async ValueTask<KillMonsterRes> KillMonsterAsync(
        KillMonsterReq request,
        CancellationToken cancellationToken)
    {
        var published = await StoreAndPublishAsync(
            GameplayDomain.CreateMonsterKilled(request.PlayerId, request.MonsterId, request.AreaId, request.IdempotencyKey, _apiName),
            cancellationToken);
        return new KillMonsterRes(published.EventId);
    }

    public async ValueTask<CollectItemRes> CollectItemAsync(
        CollectItemReq request,
        CancellationToken cancellationToken)
    {
        var published = await StoreAndPublishAsync(
            GameplayDomain.CreateItemCollected(request.PlayerId, request.ItemId, request.Count, request.IdempotencyKey, _apiName),
            cancellationToken);
        return new CollectItemRes(published.EventId);
    }

    public async ValueTask<CompleteMissionRes> CompleteMissionAsync(
        CompleteMissionReq request,
        CancellationToken cancellationToken)
    {
        var published = await StoreAndPublishAsync(
            GameplayDomain.CreateMissionCompleted(request.PlayerId, request.MissionId, request.IdempotencyKey, _apiName),
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
        var missionBaseUrl = GameQuestRouting.OwnerIndex(playerId) == 1
            ? topology.MissionBHttpBaseUrl
            : topology.MissionAHttpBaseUrl;
        var response = await httpClientFactory.CreateClient()
            .PostAsJsonAsync($"{missionBaseUrl}/internal/sync", new SyncQuestProgressReq(playerId), cancellationToken);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<SyncQuestProgressRes>(cancellationToken)
               ?? throw new InvalidOperationException("QuestMission returned an empty sync response.");
    }

    private async ValueTask<GameplayEventEnvelope> StoreAndPublishAsync(
        GameplayEventEnvelope candidate,
        CancellationToken cancellationToken)
    {
        var stored = await store.GetOrAddGameplayEventAsync(candidate, cancellationToken);
        await fanout.Publish(SampleNames.FanoutChannel, SampleNames.GameplayTopic, stored)
            .Async(cancellationToken);
            logger.LogInformation("gamequest api event published api={Api} player={PlayerId} event={EventId} type={EventType} fanout={Fanout}",
                _apiName,
                stored.PlayerId,
                stored.EventId,
                stored.EventType,
                topology.FanoutPublisherEndpointForApi(_apiName));
        return stored;
    }

}
