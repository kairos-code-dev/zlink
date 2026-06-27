using System.Collections.Concurrent;
using DiscoveryRegistryHa.Server.Embedded;

namespace DiscoveryRegistryHa.Server.Embedded.Support;

internal sealed class EvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly object _fileGate = new();
    private readonly SemaphoreSlim _signal = new(0);
    private readonly string? _filePath;

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
            if (remaining <= TimeSpan.Zero)
            {
                throw new TimeoutException("Timed out waiting for discovery registry HA evidence.");
            }

            await _signal.WaitAsync(remaining, cancellationToken);
        }
    }
}
