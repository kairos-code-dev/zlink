using System.Collections.Concurrent;
using Microsoft.Extensions.Logging;
using SpotActorTransfer.Shared;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Runtime.Actors;

namespace SpotActorTransfer.ActorNode;

using Zlink.Framework.E2E.Configuration;

internal sealed record ServerOptions(
    string Rid,
    string HttpUrl,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string RouterEndpoint,
    string EvidenceFile,
    string LogDir,
    int RequestTimeoutMilliseconds)
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
        var item = new ActorEvidence(
            scenario,
            actorId,
            kind,
            value,
            NodeRid,
            DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
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

internal sealed class RuntimeEvidenceStore
{
    private readonly ConcurrentQueue<string> _items = new();

    public void Add(string marker) => _items.Enqueue(marker);

    public async ValueTask<string[]> WaitUntilAsync(
        string[] containsAll,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var snapshot = _items.ToArray();
            if (containsAll.All(expected => snapshot.Any(item =>
                    item.Contains(expected, StringComparison.Ordinal))))
                return snapshot;
            await Task.Delay(50, cancellationToken);
        }

        return _items.ToArray();
    }
}

internal sealed class RelocationMessageFlowEvidenceStore
    : IZLinkRuntimeMessageFlowObserver
{
    private readonly ConcurrentQueue<RelocationMessageFlowEvidence>
        _items = new();

    public ValueTask OnMessageFlowAsync(
        ZLinkRuntimeMessageFlowEvent flow,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (flow.Surface == "spot"
            && flow.MessageKind == "request"
            && flow.PacketName == nameof(RelocationWorkloadRequest)
            && flow.Phase is "received" or "replied")
        {
            _items.Enqueue(new RelocationMessageFlowEvidence(
                flow.Timestamp.ToUnixTimeMilliseconds(),
                flow.Phase,
                flow.Surface,
                flow.MessageKind,
                flow.PacketName,
                flow.SpotId,
                flow.ActorId,
                flow.CorrelationId,
                flow.FlowId));
        }

        return ValueTask.CompletedTask;
    }

    public RelocationMessageFlowEvidence[] Snapshot() =>
        _items.ToArray();
}

internal sealed class TransportDeliveryGate
    : IZLinkActorTransportDeliveryGate
{
    private readonly ConcurrentDictionary<string, Entry> _entries =
        new(StringComparer.Ordinal);

    public TransportDeliveryGateRes Arm(
        string operationId,
        TransportDeliveryArmReq request)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(operationId);
        if (!Guid.TryParseExact(operationId, "N", out var parsed)
            || !string.Equals(
                operationId,
                parsed.ToString("N"),
                StringComparison.Ordinal))
            throw new ArgumentException(
                "Transport delivery operation ID must be a 128-bit lowercase GUID.",
                nameof(operationId));
        ArgumentException.ThrowIfNullOrWhiteSpace(request.ActorId);
        var expectedKind = Enum.Parse<ZLinkActorTransportOperationKind>(
            request.Kind,
            ignoreCase: true);
        var entry = new Entry(request.ActorId, expectedKind);
        if (!_entries.TryAdd(operationId, entry))
            throw new InvalidOperationException(
                $"Transport delivery operation '{operationId}' is already armed.");
        return Snapshot(operationId, entry);
    }

    public async ValueTask<TransportDeliveryGateRes> WaitCapturedAsync(
        string operationId,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var entry = Get(operationId);
        await entry.Captured.Task
            .WaitAsync(timeout, cancellationToken)
            .ConfigureAwait(false);
        return Snapshot(operationId, entry);
    }

    public TransportDeliveryGateRes Release(string operationId)
    {
        var entry = Get(operationId);
        entry.Release.TrySetResult();
        Volatile.Write(ref entry.Released, 1);
        return Snapshot(operationId, entry);
    }

    public TransportDeliveryGateRes GetSnapshot(string operationId)
    {
        var entry = Get(operationId);
        return Snapshot(operationId, entry);
    }

    public async ValueTask WaitAsync(
        ZLinkActorTransportDelivery delivery,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrEmpty(delivery.OperationId)
            || !_entries.TryGetValue(delivery.OperationId, out var entry))
            return;
        if (!string.Equals(
                entry.ActorId,
                delivery.ActorId,
                StringComparison.Ordinal)
            || entry.Kind != delivery.Kind)
            throw new InvalidOperationException(
                $"Transport delivery operation '{delivery.OperationId}' "
                + "did not match its armed Actor and operation kind.");

        Interlocked.Increment(ref entry.CapturedCount);
        entry.Captured.TrySetResult();
        await entry.Release.Task
            .WaitAsync(cancellationToken)
            .ConfigureAwait(false);
        Interlocked.Increment(ref entry.ReleasedCount);
    }

    private Entry Get(string operationId) =>
        _entries.TryGetValue(operationId, out var entry)
            ? entry
            : throw new KeyNotFoundException(
                $"Transport delivery operation '{operationId}' is not armed.");

    private static TransportDeliveryGateRes Snapshot(
        string operationId,
        Entry entry) =>
        new(
            operationId,
            entry.ActorId,
            entry.Kind.ToString(),
            Volatile.Read(ref entry.CapturedCount),
            Volatile.Read(ref entry.ReleasedCount),
            true,
            Volatile.Read(ref entry.Released) != 0);

    private sealed class Entry(
        string actorId,
        ZLinkActorTransportOperationKind kind)
    {
        internal string ActorId { get; } = actorId;
        internal ZLinkActorTransportOperationKind Kind { get; } = kind;
        internal TaskCompletionSource Captured { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal TaskCompletionSource Release { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        internal int CapturedCount;
        internal int ReleasedCount;
        internal int Released;
    }
}

internal sealed class ActorHandoffEvidenceLoggerProvider(RuntimeEvidenceStore evidence) : ILoggerProvider
{
    public ILogger CreateLogger(string categoryName) =>
        string.Equals(categoryName, "Zlink.Framework.ActorHandoff", StringComparison.Ordinal)
            ? new ActorHandoffEvidenceLogger(evidence)
            : Microsoft.Extensions.Logging.Abstractions.NullLogger.Instance;

    public void Dispose()
    {
    }

    private sealed class ActorHandoffEvidenceLogger(RuntimeEvidenceStore evidence) : ILogger
    {
        public IDisposable? BeginScope<TState>(TState state) where TState : notnull => null;

        public bool IsEnabled(LogLevel logLevel) => logLevel >= LogLevel.Information;

        public void Log<TState>(LogLevel logLevel, EventId eventId, TState state,
            Exception? exception, Func<TState, Exception?, string> formatter)
        {
            if (IsEnabled(logLevel)) evidence.Add(formatter(state, exception));
        }
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

    public Task WaitAsync(string spotId, CancellationToken cancellationToken)
    {
        var gate = _gates.GetOrAdd(
            spotId,
            static _ => new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously));
        return gate.Task.WaitAsync(cancellationToken);
    }

    public bool Release(string spotId)
    {
        var gate = _gates.GetOrAdd(
            spotId,
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
