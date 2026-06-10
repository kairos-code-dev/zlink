using GameQuest.QuestMission.Adapters.Store;
using GameQuest.QuestMission.Adapters.ZLink.Spots;
using GameQuest.QuestMission.Domain;
using GameQuest.Shared;
using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Spots;

namespace GameQuest.QuestMission.Application;

internal sealed class QuestEventProcessor(
    QuestStore store,
    IZLinkSpotManager spots,
    QuestOwnerRouter ownerRouter,
    IHttpClientFactory httpClientFactory,
    GameQuestTopology topology,
    ILogger<QuestEventProcessor> logger)
{
    private readonly QuestProjectionBuilder _projectionBuilder = new();
    private readonly string _missionName = Environment.GetEnvironmentVariable("GAMEQUEST_MISSION_NAME") ?? "mission";

    public async ValueTask ProcessAsync(
        GameplayEventEnvelope gameplayEvent,
        CancellationToken cancellationToken)
    {
        if (!ownerRouter.IsLocalOwner(gameplayEvent.PlayerId))
        {
            return;
        }

        var definition = gameplayEvent.EventType == "SnapshotKillCount"
            ? QuestCatalog.All.First(definition => definition.QuestId == QuestIds.FirstHunt)
            : QuestCatalog.Match(gameplayEvent);
        if (definition is null)
        {
            return;
        }

        await EnsurePlayerQuestSpotAsync(gameplayEvent.PlayerId, cancellationToken);
        var before = await store.ReadProgressAsync(gameplayEvent.PlayerId, definition.QuestId, cancellationToken);
        if (await store.HasSourceEventAsync(gameplayEvent.PlayerId, definition.QuestId, gameplayEvent.EventId, cancellationToken))
        {
            return;
        }

        var after = _projectionBuilder.Apply(definition, before, gameplayEvent);
        var questEvents = _projectionBuilder.ToEvents(definition, before, after, gameplayEvent);
        if (questEvents.Length == 0)
        {
            return;
        }

        if (!await store.AppendAndProjectAsync(after, questEvents, cancellationToken))
        {
            return;
        }

        var projection = await store.ReadProjectionAsync(gameplayEvent.PlayerId, cancellationToken);
        var notified = await NotifyBoundGameApiAsync(gameplayEvent.SourceApi, new NotifyQuestProgressReq(
                gameplayEvent.PlayerId,
                projection,
                questEvents.Any(e => e.EventType == nameof(QuestCompletedEvent)) ? definition.QuestId : null),
            cancellationToken);

        logger.LogInformation("gamequest mission processed mission={Mission} player={PlayerId} quest={QuestId} source={SourceEventId} events={EventCount} notified={Notified}",
            _missionName,
            gameplayEvent.PlayerId,
            definition.QuestId,
            gameplayEvent.EventId,
            questEvents.Length,
            notified);
    }

    public async ValueTask<SyncQuestProgressRes> SyncAsync(
        SyncQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        if (!ownerRouter.IsLocalOwner(request.PlayerId))
        {
            return new SyncQuestProgressRes(await store.ReadProjectionAsync(request.PlayerId, cancellationToken));
        }

        var snapshotRequest = new GetGameplaySnapshotReq(request.PlayerId);
        var response = await httpClientFactory.CreateClient()
            .PostAsJsonAsync($"{topology.GameApiAHttpBaseUrl}/internal/snapshot", snapshotRequest, cancellationToken);
        response.EnsureSuccessStatusCode();
        var snapshot = await response.Content.ReadFromJsonAsync<GetGameplaySnapshotRes>(cancellationToken)
                       ?? throw new InvalidOperationException("GameApi returned an empty gameplay snapshot.");
        var killCount = snapshot.KillCounts.Sum(kill => kill.Count);
        if (killCount > 0)
        {
            await ProcessAsync(
                new GameplayEventEnvelope(
                    $"{request.PlayerId}-snapshot-{snapshot.SnapshotVersion}",
                    request.PlayerId,
                    $"snapshot-{snapshot.SnapshotVersion}",
                    "SnapshotKillCount",
                    "kills",
                    killCount,
                    "api-a",
                    DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()),
                cancellationToken);
        }

        return new SyncQuestProgressRes(await store.ReadProjectionAsync(request.PlayerId, cancellationToken));
    }

    private async ValueTask EnsurePlayerQuestSpotAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        using var payload = new PlayerQuestSpotCreateReq(playerId).ToJson();
        var result = await spots.GetOrCreateAsync<PlayerQuestSpot>(
            RoutingId.From(System.Text.Encoding.UTF8.GetBytes($"player:{playerId}")),
            payload,
            cancellationToken);
        if (result.State == ZLinkSpotCreateState.Rejected)
        {
            throw new InvalidOperationException($"Player quest spot for '{playerId}' was rejected.");
        }
    }

    private async ValueTask<bool> NotifyBoundGameApiAsync(
        string sourceApi,
        NotifyQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        var baseUrl = string.Equals(sourceApi, "api-b", StringComparison.Ordinal)
            ? topology.GameApiBHttpBaseUrl
            : topology.GameApiAHttpBaseUrl;
        try
        {
            var response = await httpClientFactory.CreateClient()
                .PostAsJsonAsync($"{baseUrl}/internal/notify", request, cancellationToken);
            if (!response.IsSuccessStatusCode)
            {
                logger.LogInformation(
                    "gamequest mission projection kept while stream notify failed. player={PlayerId} status={StatusCode}",
                    request.PlayerId,
                    (int)response.StatusCode);
                return false;
            }

            var delivered = await response.Content.ReadFromJsonAsync<NotifyQuestProgressRes>(cancellationToken);
            return delivered?.Delivered ?? false;
        }
        catch (HttpRequestException error)
        {
            logger.LogInformation(error,
                "gamequest mission projection kept while stream notify endpoint was unavailable. player={PlayerId}",
                request.PlayerId);
            return false;
        }
    }
}
