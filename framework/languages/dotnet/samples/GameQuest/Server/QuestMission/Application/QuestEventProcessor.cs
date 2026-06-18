using System.Text.Json;
using GameQuest.QuestMission.Adapters.Store;
using GameQuest.QuestMission.Adapters.ZLink.Spots;
using GameQuest.QuestMission.Domain;
using GameQuest.Shared;
using GameQuest.Server.Configuration;
using Systems.Zlink;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Errors;
using Zlink.HttpClient;

namespace GameQuest.QuestMission.Application;

internal sealed class QuestEventProcessor(
    QuestStore store,
    IZLinkSpotManager spots,
    QuestOwnerRouter ownerRouter,
    GameQuestTopology topology,
    ILogger<QuestEventProcessor> logger)
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
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
        using var gameApi = ZLinkHttpClient.Create(topology.GameApiAHttpBaseUrl).Json().Build();
        var snapshot = (await gameApi.Post("/internal/snapshot")
            .Body(snapshotRequest)
            .SubmitAsync<GetGameplaySnapshotRes>(cancellationToken)).Body;
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
            using var gameApi = ZLinkHttpClient.Create(baseUrl).Json().Build();
            var response = await gameApi.Post("/internal/notify")
                .Body(request)
                .SubmitRawAsync(cancellationToken);
            if (response.Status is < 200 or >= 300)
            {
                logger.LogInformation(
                    "gamequest mission projection kept while stream notify failed. player={PlayerId} status={StatusCode}",
                    request.PlayerId,
                    response.Status);
                return false;
            }

            var delivered = JsonSerializer.Deserialize<NotifyQuestProgressRes>(response.Body, JsonOptions);
            return delivered?.Delivered ?? false;
        }
        catch (ZLinkFrameworkException error)
        {
            logger.LogInformation(error,
                "gamequest mission projection kept while stream notify endpoint was unavailable. player={PlayerId}",
                request.PlayerId);
            return false;
        }
    }
}
