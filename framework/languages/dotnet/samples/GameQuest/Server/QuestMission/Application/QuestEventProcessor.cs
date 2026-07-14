using GameQuest.QuestMission.Domain;
using GameQuest.Server.Configuration;
using GameQuest.Shared;

namespace GameQuest.QuestMission.Application;

internal sealed class QuestEventProcessor(
    IQuestStore store,
    IGameApiSnapshotClient snapshots,
    IQuestProgressNotifier notifications,
    QuestMissionInstanceTopology instance,
    ILogger<QuestEventProcessor> logger)
{
    private readonly string _missionName = instance.MissionName;
    private readonly QuestProjectionBuilder _projectionBuilder = new();

    public async ValueTask ProcessAsync(
        GameplayEventEnvelope gameplayEvent,
        CancellationToken cancellationToken)
    {
        var definition = gameplayEvent.EventType == "SnapshotKillCount"
            ? QuestCatalog.All.First(definition => definition.QuestId == QuestIds.FirstHunt)
            : QuestCatalog.Match(gameplayEvent);
        if (definition is null) return;

        var stream = await store.ReadQuestStreamAsync(gameplayEvent.PlayerId, definition.QuestId, cancellationToken);
        var before = _projectionBuilder.Replay(definition, stream);
        if (await store.HasSourceEventAsync(gameplayEvent.PlayerId, definition.QuestId, gameplayEvent.EventId,
                cancellationToken)) return;

        var after = _projectionBuilder.Apply(definition, before, gameplayEvent);
        var questEvents = _projectionBuilder.ToEvents(definition, before, after, gameplayEvent);
        if (questEvents.Length == 0) return;

        if (!await store.AppendAndProjectAsync(after, questEvents, cancellationToken)) return;

        var projection = await store.ReadProjectionAsync(gameplayEvent.PlayerId, cancellationToken);
        var notified = await NotifyBoundGameApiAsync(gameplayEvent.SourceApi, new NotifyQuestProgressReq(
                gameplayEvent.PlayerId,
                projection,
                questEvents.Any(e => e.EventType == nameof(QuestCompletedEvent)) ? definition.QuestId : null),
            cancellationToken);

        logger.LogInformation(
            "gamequest mission processed mission={Mission} player={PlayerId} quest={QuestId} source={SourceEventId} events={EventCount} notified={Notified}",
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
        var snapshotRequest = new GetGameplaySnapshotReq(request.PlayerId);
        var snapshot = await snapshots.ReadSnapshotAsync(snapshotRequest, cancellationToken);
        var killCount = snapshot.KillCounts.Sum(kill => kill.Count);
        if (killCount > 0)
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

        return new SyncQuestProgressRes(await store.ReadProjectionAsync(request.PlayerId, cancellationToken));
    }

    private async ValueTask<bool> NotifyBoundGameApiAsync(
        string sourceApi,
        NotifyQuestProgressReq request,
        CancellationToken cancellationToken)
    {
        var result = await notifications.NotifyAsync(sourceApi, request, cancellationToken);
        if (result.FailureStatus is not null)
            logger.LogWarning(
                "gamequest mission projection kept while stream notify failed. player={PlayerId} status={StatusCode}",
                request.PlayerId,
                result.FailureStatus);
        else if (result.UnavailableError is not null)
            logger.LogWarning(result.UnavailableError,
                "gamequest mission projection kept while stream notify endpoint was unavailable. player={PlayerId}",
                request.PlayerId);

        return result.Delivered;
    }
}

internal interface IQuestStore
{
    ValueTask<QuestProgress[]> ReadProjectionAsync(
        string playerId,
        CancellationToken cancellationToken);

    ValueTask<StoredQuestEvent[]> ReadQuestStreamAsync(
        string playerId,
        string questId,
        CancellationToken cancellationToken);

    ValueTask<bool> HasSourceEventAsync(
        string playerId,
        string questId,
        string sourceEventId,
        CancellationToken cancellationToken);

    ValueTask<bool> AppendAndProjectAsync(
        QuestProgress projection,
        IReadOnlyList<StoredQuestEvent> events,
        CancellationToken cancellationToken);
}

internal interface IGameApiSnapshotClient
{
    ValueTask<GetGameplaySnapshotRes> ReadSnapshotAsync(
        GetGameplaySnapshotReq request,
        CancellationToken cancellationToken);
}

internal interface IQuestProgressNotifier
{
    ValueTask<QuestProgressNotifyResult> NotifyAsync(
        string sourceApi,
        NotifyQuestProgressReq request,
        CancellationToken cancellationToken);
}

internal sealed record QuestProgressNotifyResult(
    bool Delivered,
    int? FailureStatus,
    Exception? UnavailableError);
