using System.Collections.Concurrent;

namespace ResilienceLifecycle.Server.Provider;

internal sealed class FaultState
{
    public string Mode { get; set; } = "none";
}

internal sealed class EvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly object _fileGate = new();
    private readonly string? _filePath;
    private readonly object _waiterGate = new();
    private readonly List<TaskCompletionSource> _waiters = new();

    public EvidenceStore(string? filePath)
    {
        _filePath = filePath;
        Rid = Environment.GetEnvironmentVariable("ZLINK_E2E_RID") ?? "node";
        if (!string.IsNullOrWhiteSpace(_filePath))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_filePath)!);
            File.WriteAllText(_filePath, string.Empty);
        }
    }

    public string Rid { get; }

    public void Add(string entry)
    {
        _entries.Enqueue(entry);
        SignalWaiters();
        if (string.IsNullOrWhiteSpace(_filePath)) return;

        lock (_fileGate)
        {
            File.AppendAllText(_filePath, entry + Environment.NewLine);
        }
    }

    public string[] Snapshot()
    {
        return _entries.ToArray();
    }

    public async Task<string[]> WaitUntilAsync(
        Func<string[], bool> predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (true)
        {
            var waiter = AddWaiter();
            var snapshot = Snapshot();
            if (predicate(snapshot))
            {
                RemoveWaiter(waiter);
                return snapshot;
            }

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero)
            {
                RemoveWaiter(waiter);
                throw new TimeoutException("Timed out waiting for resilience lifecycle evidence.");
            }

            try
            {
                await waiter.Task.WaitAsync(remaining, cancellationToken);
            }
            catch
            {
                RemoveWaiter(waiter);
                throw;
            }
        }
    }

    public void Clear()
    {
        while (_entries.TryDequeue(out _))
        {
        }

        if (!string.IsNullOrWhiteSpace(_filePath))
            lock (_fileGate)
            {
                File.WriteAllText(_filePath, string.Empty);
            }
    }

    private TaskCompletionSource AddWaiter()
    {
        var waiter = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        lock (_waiterGate)
        {
            _waiters.Add(waiter);
        }

        return waiter;
    }

    private void RemoveWaiter(TaskCompletionSource waiter)
    {
        lock (_waiterGate)
        {
            _waiters.Remove(waiter);
        }
    }

    private void SignalWaiters()
    {
        TaskCompletionSource[] waiters;
        lock (_waiterGate)
        {
            waiters = _waiters.ToArray();
            _waiters.Clear();
        }

        foreach (var waiter in waiters) waiter.TrySetResult();
    }
}

internal sealed record ServerOptions(
    string Role,
    string Rid,
    string HttpUrl,
    string LogDir,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    string? ChannelEndpoint,
    string? EvidenceFile,
    int Weight)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
    {
        var values = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException($"Unexpected argument '{key}'.");

            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{key}'.");

            var value = args[++i];
            if (!values.TryGetValue(key, out var bucket))
            {
                bucket = [];
                values.Add(key, bucket);
            }

            bucket.Add(value);
        }

        string? Get(string name)
        {
            return values.TryGetValue(name, out var bucket) ? bucket[^1] : null;
        }

        return new ServerOptions(
            defaultRole,
            Get("--rid") ?? "node",
            Get("--http-url") ?? "http://127.0.0.1:0",
            Get("--log-dir") ?? "logs",
            Get("--registry-pub-endpoint"),
            Get("--registry-router-endpoint"),
            Get("--channel-endpoint"),
            Get("--evidence-file"),
            int.TryParse(Get("--weight"), out var weight) ? weight : 100);
    }
}