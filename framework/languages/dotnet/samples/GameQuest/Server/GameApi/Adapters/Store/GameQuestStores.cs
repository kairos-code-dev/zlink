using System.Text.Json;
using GameQuest.Shared;
using GameQuest.Server.Configuration;

namespace GameQuest.GameApi.Adapters.Store;

internal sealed class GameQuestStore
{
    private readonly string _directory;
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    public GameQuestStore(GameQuestTopology topology)
    {
        _directory = topology.StoreDirectory;
        Directory.CreateDirectory(_directory);
    }

    public async ValueTask<GameplayEventEnvelope> GetOrAddGameplayEventAsync(
        GameplayEventEnvelope candidate,
        CancellationToken cancellationToken)
    {
        var path = Path.Combine(_directory, "gameplay-events.json");
        return await UpdateAsync(
            path,
            new List<GameplayEventEnvelope>(),
            events =>
            {
                var existing = events.FirstOrDefault(e =>
                    string.Equals(e.PlayerId, candidate.PlayerId, StringComparison.Ordinal)
                    && string.Equals(e.IdempotencyKey, candidate.IdempotencyKey, StringComparison.Ordinal));
                if (existing is not null)
                {
                    return existing;
                }

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
        var path = Path.Combine(_directory, "unpublished-kills.json");
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
            Path.Combine(_directory, "gameplay-events.json"),
            [],
            cancellationToken);
        var unpublished = await ReadAsync<Dictionary<string, int>>(
            Path.Combine(_directory, "unpublished-kills.json"),
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
            Path.Combine(_directory, "gameplay-events.json"),
            [],
            cancellationToken);
        var unpublishedKills = await ReadAsync<Dictionary<string, int>>(
            Path.Combine(_directory, "unpublished-kills.json"),
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
            Path.Combine(_directory, "quest-subscriptions.json"),
            new List<BindQuestSessionReq>(),
            bindings =>
            {
                bindings.RemoveAll(binding => binding.PlayerId == request.PlayerId);
                bindings.Add(request);
                return true;
            },
            cancellationToken);
        await UpdateAsync(
            Path.Combine(_directory, "quest-subscription-history.json"),
            new List<BindQuestSessionReq>(),
            bindings =>
            {
                if (bindings.All(binding =>
                        binding.PlayerId != request.PlayerId
                        || binding.ConnectionId != request.ConnectionId))
                {
                    bindings.Add(request);
                }

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
            Path.Combine(_directory, "quest-subscriptions.json"),
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
            Path.Combine(_directory, "quest-subscriptions.json"),
            [],
            cancellationToken);
        return bindings.OrderBy(binding => binding.PlayerId, StringComparer.Ordinal).ToArray();
    }

    public async ValueTask<BindQuestSessionReq[]> ReadBindingHistoryAsync(CancellationToken cancellationToken)
    {
        var bindings = await ReadAsync<List<BindQuestSessionReq>>(
            Path.Combine(_directory, "quest-subscription-history.json"),
            [],
            cancellationToken);
        return bindings.OrderBy(binding => binding.PlayerId, StringComparer.Ordinal).ToArray();
    }

    public async ValueTask<QuestProgress[]> ReadProjectionAsync(
        string playerId,
        CancellationToken cancellationToken)
    {
        var all = await ReadAsync<List<QuestProgress>>(
            Path.Combine(_directory, "quest-projection.json"),
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
            Path.Combine(_directory, "quest-projection.json"),
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
        {
            throw new InvalidOperationException($"Quest stream was not found for {playerId}/{questId}.");
        }

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
                if (root.TryGetProperty("RequiredCount", out var required))
                {
                    requiredCount = required.GetInt32();
                }
            }

            if (@event.EventType == nameof(QuestCompletedEvent))
            {
                status = QuestStatuses.Completed;
            }
            else if (@event.EventType == nameof(QuestRewardGrantedEvent))
            {
                status = QuestStatuses.RewardGranted;
            }

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
            Path.Combine(_directory, "quest-projection.json"),
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
            Path.Combine(_directory, "quest-events.json"),
            [],
            cancellationToken);
        return events
            .OrderBy(e => e.PlayerId, StringComparer.Ordinal)
            .ThenBy(e => e.QuestId, StringComparer.Ordinal)
            .ThenBy(e => e.Version)
            .ToArray();
    }

    private static async ValueTask<TResult> UpdateAsync<T, TResult>(
        string path,
        T fallback,
        Func<T, TResult> update,
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var mutex = new Mutex(false, MutexName(path));
        mutex.WaitOne();
        try
        {
            var value = await ReadAsync(path, fallback, cancellationToken);
            var result = update(value);
            File.WriteAllText(path, JsonSerializer.Serialize(value, JsonOptions));
            return result;
        }
        finally
        {
            mutex.ReleaseMutex();
        }
    }

    private static async ValueTask<T> ReadAsync<T>(
        string path,
        T fallback,
        CancellationToken cancellationToken)
    {
        if (!File.Exists(path))
        {
            return fallback;
        }

        await Task.CompletedTask;
        var json = File.ReadAllText(path);
        return JsonSerializer.Deserialize<T>(json, JsonOptions) ?? fallback;
    }

    private static string MutexName(string path) =>
        "GameQuest-" + Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(path)));
}
