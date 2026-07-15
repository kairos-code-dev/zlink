using System.Collections.Concurrent;

namespace LocationMessaging.Server.Workflow.Infrastructure;

internal sealed class EvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly object _fileGate = new();
    private readonly string? _filePath;
    private readonly SemaphoreSlim _signal = new(0);

    public EvidenceStore(string rid, string? filePath)
    {
        _filePath = filePath;
        Rid = rid;
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
        _signal.Release();
        if (string.IsNullOrWhiteSpace(_filePath))
        {
            return;
        }

        lock (_fileGate)
        {
            File.AppendAllText(_filePath, entry + Environment.NewLine);
        }
    }

    public string[] Snapshot() => _entries.ToArray();

    public async Task<string[]> WaitUntilAsync(
        Func<string, bool> predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (true)
        {
            var snapshot = Snapshot();
            if (snapshot.Any(predicate))
            {
                return snapshot;
            }

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero
                || !await _signal.WaitAsync(remaining, cancellationToken))
            {
                return Snapshot();
            }
        }
    }

    public void Clear()
    {
        while (_entries.TryDequeue(out _))
        {
        }

        if (!string.IsNullOrWhiteSpace(_filePath))
        {
            lock (_fileGate)
            {
                File.WriteAllText(_filePath, string.Empty);
            }
        }
    }
}
