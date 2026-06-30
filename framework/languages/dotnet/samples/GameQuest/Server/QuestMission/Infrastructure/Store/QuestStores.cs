using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using GameQuest.QuestMission.Application;
using GameQuest.Server.Configuration;
using GameQuest.Shared;

namespace GameQuest.QuestMission.Infrastructure.Store;

internal sealed class QuestStore : IQuestStore
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);
    private readonly string _directory;

    public QuestStore(GameQuestTopology topology)
    {
        _directory = topology.StoreDirectory;
        Directory.CreateDirectory(_directory);
    }

    public async ValueTask<QuestProgress?> ReadProgressAsync(
        string playerId,
        string questId,
        CancellationToken cancellationToken)
    {
        var all = await ReadProjectionAsync(playerId, cancellationToken);
        return all.FirstOrDefault(p => p.QuestId == questId);
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

    public async ValueTask<bool> HasSourceEventAsync(
        string playerId,
        string questId,
        string sourceEventId,
        CancellationToken cancellationToken)
    {
        var events = await ReadEventsAsync(cancellationToken);
        return events.Any(e =>
            e.PlayerId == playerId
            && e.QuestId == questId
            && e.SourceEventId == sourceEventId);
    }

    public async ValueTask<bool> AppendAndProjectAsync(
        QuestProgress progress,
        IReadOnlyList<StoredQuestEvent> events,
        CancellationToken cancellationToken)
    {
        var appended = false;
        await UpdateAsync(
            Path.Combine(_directory, "quest-events.json"),
            new List<StoredQuestEvent>(),
            stored =>
            {
                if (stored.Any(existing =>
                        existing.PlayerId == progress.PlayerId
                        && existing.QuestId == progress.QuestId
                        && existing.SourceEventId == progress.LastEventId))
                    return;

                var nextVersion = stored
                    .Where(existing => existing.PlayerId == progress.PlayerId && existing.QuestId == progress.QuestId)
                    .Select(existing => existing.Version)
                    .DefaultIfEmpty(0)
                    .Max() + 1;
                foreach (var @event in events)
                    if (stored.All(existing => existing.EventId != @event.EventId))
                    {
                        stored.Add(@event with { Version = nextVersion++ });
                        appended = true;
                    }
            },
            cancellationToken);

        if (!appended) return false;

        await UpdateAsync(
            Path.Combine(_directory, "quest-projection.json"),
            new List<QuestProgress>(),
            projection =>
            {
                projection.RemoveAll(p => p.PlayerId == progress.PlayerId && p.QuestId == progress.QuestId);
                projection.Add(progress);
            },
            cancellationToken);
        return true;
    }

    public async ValueTask<StoredQuestEvent[]> ReadEventsAsync(CancellationToken cancellationToken)
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

    private static async ValueTask UpdateAsync<T>(
        string path,
        T fallback,
        Action<T> update,
        CancellationToken cancellationToken)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        using var mutex = new Mutex(false, MutexName(path));
        mutex.WaitOne();
        try
        {
            var value = await ReadAsync(path, fallback, cancellationToken);
            update(value);
            File.WriteAllText(path, JsonSerializer.Serialize(value, JsonOptions));
        }
        finally
        {
            mutex.ReleaseMutex();
        }
    }

    private static async ValueTask<T> ReadAsync<T>(string path, T fallback, CancellationToken cancellationToken)
    {
        if (!File.Exists(path)) return fallback;

        await Task.CompletedTask;
        var json = File.ReadAllText(path);
        return JsonSerializer.Deserialize<T>(json, JsonOptions) ?? fallback;
    }

    private static string MutexName(string path)
    {
        return "GameQuest-" +
               Convert.ToHexString(
                   SHA256.HashData(Encoding.UTF8.GetBytes(path)));
    }
}