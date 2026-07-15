using System.Collections.Concurrent;
using SpotActorTransfer.Shared;

namespace SpotActorTransfer.ActorNode;

internal sealed record ServerOptions(
    string Rid,
    string HttpUrl,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string RouterEndpoint,
    string EvidenceFile,
    string LogDir)
{
    public static ServerOptions Parse(string[] args, string role)
    {
        var values = ParseArgs(args);
        var logDir = Get(values, "log-dir", Path.Combine(AppContext.BaseDirectory, "logs"));
        return new ServerOptions(
            Get(values, "rid", role),
            Get(values, "http-url", "http://127.0.0.1:0"),
            Get(values, "redis-endpoint", "127.0.0.1:6379"),
            Get(values, "redis-key-prefix", "zlink:e2e:spot-actor-transfer"),
            Get(values, "router-endpoint", "tcp://127.0.0.1:0"),
            Get(values, "evidence-file", Path.Combine(logDir, $"{role}.evidence.log")),
            logDir);
    }

    private static Dictionary<string, string> ParseArgs(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i += 2)
        {
            var key = args[i].TrimStart('-');
            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{args[i]}'.");
            values[key] = args[i + 1];
        }

        return values;
    }

    private static string Get(Dictionary<string, string> values, string key, string fallback) =>
        values.TryGetValue(key, out var value) ? value : fallback;
}

internal sealed class EvidenceStore(string nodeRid, string path)
{
    private readonly ConcurrentQueue<ActorEvidence> _items = new();

    public string NodeRid { get; } = nodeRid;

    public void Add(string scenario, string actorId, string kind, string value)
    {
        var item = new ActorEvidence(scenario, actorId, kind, value, NodeRid);
        _items.Enqueue(item);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.AppendAllLines(path, [$"{item.Scenario}|{item.ActorId}|{item.Kind}|{item.Value}|{item.NodeRid}"]);
    }

    public ActorEvidence[] Snapshot() => _items.ToArray();

    public async ValueTask<ActorEvidence[]> WaitUntilAsync(
        Func<ActorEvidence[], bool> predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var snapshot = Snapshot();
            if (predicate(snapshot)) return snapshot;
            await Task.Delay(100, cancellationToken);
        }

        return Snapshot();
    }
}

internal sealed class DomainStateStore(string directory)
{
    public void Save(string actorId, int stateVersion)
    {
        Directory.CreateDirectory(directory);
        File.WriteAllText(Path.Combine(directory, $"domain-{actorId}.state"), stateVersion.ToString());
    }

    public int Load(string actorId)
    {
        var path = Path.Combine(directory, $"domain-{actorId}.state");
        return int.Parse(File.ReadAllText(path));
    }
}

internal sealed class JoinedGateStore
{
    private readonly ConcurrentDictionary<string, TaskCompletionSource> _gates = new(StringComparer.Ordinal);

    public Task WaitAsync(string spotRid, CancellationToken cancellationToken)
    {
        var gate = _gates.GetOrAdd(
            spotRid,
            static _ => new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously));
        return gate.Task.WaitAsync(cancellationToken);
    }

    public bool Release(string spotRid)
    {
        var gate = _gates.GetOrAdd(
            spotRid,
            static _ => new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously));
        return gate.TrySetResult();
    }
}

internal sealed class TransferGateStore
{
    private readonly ConcurrentDictionary<string, TaskCompletionSource> _gates = new(StringComparer.Ordinal);

    public Task WaitAsync(string actorId, CancellationToken cancellationToken)
    {
        var gate = _gates.GetOrAdd(
            actorId,
            static _ => new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously));
        return gate.Task.WaitAsync(cancellationToken);
    }

    public bool Release(string actorId)
    {
        var gate = _gates.GetOrAdd(
            actorId,
            static _ => new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously));
        return gate.TrySetResult();
    }
}
