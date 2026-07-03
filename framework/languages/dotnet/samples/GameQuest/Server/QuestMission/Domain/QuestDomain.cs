using System.Text.Json;
using GameQuest.Server.Configuration;
using GameQuest.Shared;

namespace GameQuest.QuestMission.Domain;

internal sealed record QuestDefinition(string QuestId, string EventType, string Value, int Required);

internal static class QuestCatalog
{
    public static readonly QuestDefinition[] All =
    [
        new(QuestIds.FirstHunt, "MonsterKilled", "*", 3),
        new(QuestIds.OpenAuction, "FeatureUnlocked", "auction", 1),
        new(QuestIds.HerbGathering, "ItemCollected", "healing-herb", 5),
        new(QuestIds.ClearTutorial, "MissionCompleted", "tutorial", 1),
        new(QuestIds.VisitRuins, "AreaEntered", "ruins", 1)
    ];

    public static QuestDefinition? Match(GameplayEventEnvelope gameplayEvent)
    {
        return All.FirstOrDefault(definition =>
            definition.EventType == gameplayEvent.EventType
            && (definition.Value == "*" || definition.Value == gameplayEvent.Value));
    }
}

internal sealed class QuestProjectionBuilder
{
    public QuestProgress? Replay(QuestDefinition definition, IReadOnlyList<StoredQuestEvent> stream)
    {
        if (stream.Count == 0) return null;

        var currentCount = 0;
        var status = QuestStatuses.Active;
        string? lastEventId = null;
        long updatedAtUnixMs = 0;
        foreach (var @event in stream.OrderBy(e => e.Version))
        {
            using var payload = JsonDocument.Parse(@event.Payload);
            var root = payload.RootElement;
            if (@event.EventType is nameof(QuestProgressedEvent) or nameof(QuestProgressReconciledEvent))
            {
                currentCount = root.GetProperty("CurrentCount").GetInt32();
                if (@event.EventType == nameof(QuestProgressedEvent)
                    && root.TryGetProperty("RequiredCount", out var required))
                    definition = definition with { Required = required.GetInt32() };
            }

            if (@event.EventType == nameof(QuestCompletedEvent))
                status = QuestStatuses.Completed;
            else if (@event.EventType == nameof(QuestRewardGrantedEvent))
                status = QuestStatuses.RewardGranted;

            lastEventId = @event.SourceEventId;
            updatedAtUnixMs = Math.Max(updatedAtUnixMs, @event.CreatedAtUnixMs);
        }

        return new QuestProgress(
            stream[0].PlayerId,
            definition.QuestId,
            status,
            currentCount,
            definition.Required,
            lastEventId,
            updatedAtUnixMs);
    }

    public QuestProgress Apply(QuestDefinition definition, QuestProgress? current, GameplayEventEnvelope gameplayEvent)
    {
        var nextCount = gameplayEvent.EventType == "SnapshotKillCount"
            ? Math.Max(current?.CurrentCount ?? 0, gameplayEvent.Count)
            : Math.Min(definition.Required, (current?.CurrentCount ?? 0) + gameplayEvent.Count);
        var complete = nextCount >= definition.Required;
        return new QuestProgress(
            gameplayEvent.PlayerId,
            definition.QuestId,
            complete ? QuestStatuses.RewardGranted : QuestStatuses.Active,
            nextCount,
            definition.Required,
            gameplayEvent.EventId,
            DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
    }

    public StoredQuestEvent[] ToEvents(
        QuestDefinition definition,
        QuestProgress? before,
        QuestProgress after,
        GameplayEventEnvelope source)
    {
        if (before?.LastEventId == source.EventId) return [];

        if (before is not null
            && before.CurrentCount == after.CurrentCount
            && before.Status == after.Status)
            return [];

        var events = new List<StoredQuestEvent>();
        var kind = source.EventType == "SnapshotKillCount"
            ? nameof(QuestProgressReconciledEvent)
            : nameof(QuestProgressedEvent);
        events.Add(Create(kind));

        if (before?.Status != QuestStatuses.RewardGranted && after.Status == QuestStatuses.RewardGranted)
        {
            events.Add(Create(nameof(QuestCompletedEvent)));
            events.Add(Create(nameof(QuestRewardGrantedEvent)));
        }

        return events.ToArray();

        StoredQuestEvent Create(string eventType)
        {
            var now = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
            object payload = eventType switch
            {
                nameof(QuestProgressedEvent) => new QuestProgressedEvent(
                    $"{after.PlayerId}-{definition.QuestId}-{source.EventId}-{eventType}",
                    after.PlayerId,
                    definition.QuestId,
                    Math.Max(0, after.CurrentCount - (before?.CurrentCount ?? 0)),
                    after.CurrentCount,
                    after.RequiredCount,
                    source.EventId),
                nameof(QuestCompletedEvent) => new QuestCompletedEvent(
                    $"{after.PlayerId}-{definition.QuestId}-{source.EventId}-{eventType}",
                    after.PlayerId,
                    definition.QuestId,
                    source.EventId,
                    now),
                nameof(QuestRewardGrantedEvent) => new QuestRewardGrantedEvent(
                    $"{after.PlayerId}-{definition.QuestId}-{source.EventId}-{eventType}",
                    after.PlayerId,
                    definition.QuestId,
                    source.EventId,
                    $"reward-{definition.QuestId}",
                    now),
                nameof(QuestProgressReconciledEvent) => new QuestProgressReconciledEvent(
                    $"{after.PlayerId}-{definition.QuestId}-{source.EventId}-{eventType}",
                    after.PlayerId,
                    definition.QuestId,
                    after.CurrentCount,
                    "GameplaySnapshot",
                    now),
                _ => new { after.PlayerId, after.QuestId, SourceEventId = source.EventId }
            };
            return new StoredQuestEvent(
                $"{after.PlayerId}-{definition.QuestId}-{source.EventId}-{eventType}",
                source.EventId,
                after.PlayerId,
                definition.QuestId,
                eventType,
                JsonSerializer.SerializeToUtf8Bytes(payload),
                0,
                now);
        }
    }
}
