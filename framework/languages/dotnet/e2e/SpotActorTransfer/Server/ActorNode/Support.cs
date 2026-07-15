using System.Collections.Concurrent;
using SpotActorTransfer.Shared;

namespace SpotActorTransfer.ActorNode;

using Zlink.Framework.E2E.Configuration;

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
        => E2eConfiguration.Load<ServerOptions>(args);
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
