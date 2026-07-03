using System.Text.Json;
using GameQuest.GameApi.Application;
using GameQuest.Server.Configuration;
using GameQuest.Shared;

namespace GameQuest.GameApi.Infrastructure.Store;

internal sealed class GameQuestStore : IGameplayEventStore, IQuestSessionStore, IAsyncDisposable
{
    private readonly string _keyPrefix;
    private readonly RedisJsonStore _redis;

    public GameQuestStore(GameQuestTopology topology)
    {
        _redis = new RedisJsonStore(topology.RedisEndpoint);
        _keyPrefix = $"{topology.RedisKeyPrefix}gamequest:";
    }

    public async ValueTask DisposeAsync()
    {
        await _redis.DisposeAsync().ConfigureAwait(false);
    }

    public async ValueTask<GameplayEventEnvelope> GetOrAddGameplayEventAsync(
        GameplayEventEnvelope candidate,
        CancellationToken cancellationToken)
    {
        var path = Key("gameplay-events");
        return await UpdateAsync(
            path,
            new List<GameplayEventEnvelope>(),
            events =>
            {
                var existing = events.FirstOrDefault(e =>
                    string.Equals(e.PlayerId, candidate.PlayerId, StringComparison.Ordinal)
                    && string.Equals(e.IdempotencyKey, candidate.IdempotencyKey, StringComparison.Ordinal));
                if (existing is not null) return existing;

                events.Add(candidate);
                return candidate;
            },
            cancellationToken);
    }

    public async ValueTask AddUnpublishedKillAsync(
        string playerId,
        int count,
        CancellationToken cancellationToken)
    {
        var path = Key("unpublished-kills");
        await UpdateAsync(
            path,
            new Dictionary<string, int>(StringComparer.Ordinal),
            kills =>
            {
                kills[playerId] = kills.GetValueOrDefault(playerId) + count;
                return 0;
            },
            cancellationToken);
    }

    public async ValueTask<int> GetSnapshotKillCountAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        var events = await ReadAsync<List<GameplayEventEnvelope>>(
            Key("gameplay-events"),
            [],
            cancellationToken);
        var unpublished = await ReadAsync<Dictionary<string, int>>(
            Key("unpublished-kills"),
            new Dictionary<string, int>(StringComparer.Ordinal),
            cancellationToken);
        return events
                   .Where(e => e.PlayerId == playerId && e.EventType == "MonsterKilled")
                   .Sum(e => e.Count)
               + unpublished.GetValueOrDefault(playerId);
    }

    public async ValueTask<GetGameplaySnapshotRes> ReadSnapshotAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        var events = await ReadAsync<List<GameplayEventEnvelope>>(
            Key("gameplay-events"),
            [],
            cancellationToken);
        var unpublishedKills = await ReadAsync<Dictionary<string, int>>(
            Key("unpublished-kills"),
            new Dictionary<string, int>(StringComparer.Ordinal),
            cancellationToken);

        var killCounts = events
            .Where(e => e.PlayerId == playerId && e.EventType == "MonsterKilled")
            .GroupBy(e => new { e.Value, AreaId = (string?)null })
            .Select(group => new KillCountSnapshot(
                group.Key.Value,
                group.Key.AreaId,
                group.Sum(e => e.Count)))
            .ToList();
        if (unpublishedKills.GetValueOrDefault(playerId) > 0)
        {
            var existing = killCounts.FirstOrDefault(kill => kill.MonsterId == "wolf");
            killCounts.RemoveAll(kill => kill.MonsterId == "wolf");
            killCounts.Add(new KillCountSnapshot(
                "wolf",
                null,
                (existing?.Count ?? 0) + unpublishedKills[playerId]));
        }

        return new GetGameplaySnapshotRes(
            playerId,
            killCounts.ToArray(),
            events.Where(e => e.PlayerId == playerId && e.EventType == "ItemCollected")
                .GroupBy(e => e.Value)
                .Select(group => new ItemCountSnapshot(group.Key, group.Sum(e => e.Count)))
                .ToArray(),
            events.Where(e => e.PlayerId == playerId && e.EventType == "MissionCompleted")
                .Select(e => e.Value)
                .Distinct(StringComparer.Ordinal)
                .Order(StringComparer.Ordinal)
                .ToArray(),
            events.Where(e => e.PlayerId == playerId && e.EventType == "FeatureUnlocked")
                .Select(e => e.Value)
                .Distinct(StringComparer.Ordinal)
                .Order(StringComparer.Ordinal)
                .ToArray(),
            events.Where(e => e.PlayerId == playerId && e.EventType == "AreaEntered")
                .Select(e => e.Value)
                .Distinct(StringComparer.Ordinal)
                .Order(StringComparer.Ordinal)
                .ToArray(),
            events.Where(e => e.PlayerId == playerId).Select(e => e.CreatedAtUnixMs).DefaultIfEmpty(0).Max()
            + unpublishedKills.GetValueOrDefault(playerId));
    }

    public async ValueTask<BindQuestSessionRes> BindSessionAsync(
        BindQuestSessionReq request,
        CancellationToken cancellationToken)
    {
        await UpdateAsync(
            Key("quest-subscriptions"),
            new List<BindQuestSessionReq>(),
            bindings =>
            {
                bindings.RemoveAll(binding => binding.PlayerId == request.PlayerId);
                bindings.Add(request);
                return true;
            },
            cancellationToken);
        await UpdateAsync(
            Key("quest-subscription-history"),
            new List<BindQuestSessionReq>(),
            bindings =>
            {
                if (bindings.All(binding =>
                        binding.PlayerId != request.PlayerId
                        || binding.ConnectionId != request.ConnectionId))
                    bindings.Add(request);

                return true;
            },
            cancellationToken);
        return new BindQuestSessionRes(true);
    }

    public async ValueTask<UnbindQuestSessionRes> UnbindSessionAsync(
        UnbindQuestSessionReq request,
        CancellationToken cancellationToken)
    {
        var removed = await UpdateAsync(
            Key("quest-subscriptions"),
            new List<BindQuestSessionReq>(),
            bindings =>
            {
                var before = bindings.Count;
                bindings.RemoveAll(binding =>
                    binding.PlayerId == request.PlayerId
                    && binding.ConnectionId == request.ConnectionId);
                return bindings.Count != before;
            },
            cancellationToken);
        return new UnbindQuestSessionRes(removed);
    }

    public async ValueTask<BindQuestSessionReq[]> ReadBindingsAsync(CancellationToken cancellationToken)
    {
        var bindings = await ReadAsync<List<BindQuestSessionReq>>(
            Key("quest-subscriptions"),
            [],
            cancellationToken);
        return bindings.OrderBy(binding => binding.PlayerId, StringComparer.Ordinal).ToArray();
    }

    public async ValueTask<BindQuestSessionReq[]> ReadBindingHistoryAsync(CancellationToken cancellationToken)
    {
        var bindings = await ReadAsync<List<BindQuestSessionReq>>(
            Key("quest-subscription-history"),
            [],
            cancellationToken);
        return bindings.OrderBy(binding => binding.PlayerId, StringComparer.Ordinal).ToArray();
    }

    public async ValueTask<QuestProgress[]> ReadProjectionAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        var all = await ReadAsync<List<QuestProgress>>(
            Key("quest-projection"),
            [],
            cancellationToken);
        return all
            .Where(progress => progress.PlayerId == playerId)
            .OrderBy(progress => progress.QuestId, StringComparer.Ordinal)
            .ToArray();
    }

    public async ValueTask DeleteProjectionAsync(
        string playerId,
        string questId,
        CancellationToken cancellationToken)
    {
        await UpdateAsync(
            Key("quest-projection"),
            new List<QuestProgress>(),
            projection =>
            {
                projection.RemoveAll(progress =>
                    progress.PlayerId == playerId
                    && progress.QuestId == questId);
                return true;
            },
            cancellationToken);
    }

    public async ValueTask<QuestProgress> RebuildProjectionAsync(
        string playerId,
        string questId,
        CancellationToken cancellationToken)
    {
        var events = await ReadQuestEventsAsync(cancellationToken);
        var stream = events
            .Where(e => e.PlayerId == playerId && e.QuestId == questId)
            .OrderBy(e => e.Version)
            .ToArray();
        if (stream.Length == 0)
            throw new InvalidOperationException($"Quest stream was not found for {playerId}/{questId}.");

        var currentCount = 0;
        var requiredCount = 1;
        var status = QuestStatuses.Active;
        string? lastEventId = null;
        long updatedAtUnixMs = 0;
        foreach (var @event in stream)
        {
            using var payload = JsonDocument.Parse(@event.Payload);
            var root = payload.RootElement;
            if (@event.EventType is nameof(QuestProgressedEvent) or nameof(QuestProgressReconciledEvent))
            {
                currentCount = root.GetProperty("CurrentCount").GetInt32();
                if (root.TryGetProperty("RequiredCount", out var required)) requiredCount = required.GetInt32();
            }

            if (@event.EventType == nameof(QuestCompletedEvent))
                status = QuestStatuses.Completed;
            else if (@event.EventType == nameof(QuestRewardGrantedEvent)) status = QuestStatuses.RewardGranted;

            lastEventId = @event.SourceEventId;
            updatedAtUnixMs = Math.Max(updatedAtUnixMs, @event.CreatedAtUnixMs);
        }

        var rebuilt = new QuestProgress(
            playerId,
            questId,
            status,
            currentCount,
            requiredCount,
            lastEventId,
            updatedAtUnixMs);
        await UpdateAsync(
            Key("quest-projection"),
            new List<QuestProgress>(),
            projection =>
            {
                projection.RemoveAll(progress =>
                    progress.PlayerId == playerId
                    && progress.QuestId == questId);
                projection.Add(rebuilt);
                return true;
            },
            cancellationToken);
        return rebuilt;
    }

    public async ValueTask<StoredQuestEvent[]> ReadQuestEventsAsync(CancellationToken cancellationToken)
    {
        var events = await ReadAsync<List<StoredQuestEvent>>(
            Key("quest-events"),
            [],
            cancellationToken);
        return events
            .OrderBy(e => e.PlayerId, StringComparer.Ordinal)
            .ThenBy(e => e.QuestId, StringComparer.Ordinal)
            .ThenBy(e => e.Version)
            .ToArray();
    }

    public async ValueTask<Dictionary<string, int>> ReadOwnerRehydrateEvidenceAsync(CancellationToken cancellationToken)
    {
        return await ReadAsync<Dictionary<string, int>>(
            Key("owner-rehydrates"),
            new Dictionary<string, int>(StringComparer.Ordinal),
            cancellationToken);
    }

    private async ValueTask<TResult> UpdateAsync<T, TResult>(
        string key,
        T fallback,
        Func<T, TResult> update,
        CancellationToken cancellationToken)
    {
        return await _redis.UpdateAsync(key, fallback, update, cancellationToken).ConfigureAwait(false);
    }

    private ValueTask<T> ReadAsync<T>(
        string key,
        T fallback,
        CancellationToken cancellationToken)
    {
        return _redis.ReadAsync(key, fallback, cancellationToken);
    }

    private string Key(string name) => $"{_keyPrefix}{name}";
}
