using System.Collections.Concurrent;
using Bingo.Server.Infrastructure;

namespace Bingo.Server.Infrastructure;

public sealed class InMemoryRegistryDiscoveryMetadata : IRegistryDiscoveryMetadata
{
    private readonly ConcurrentDictionary<string, RegistryMetadataEntry> _entries = new(StringComparer.Ordinal);
    private readonly object _gate = new();

    public ValueTask PutAsync(
        string key,
        IReadOnlyDictionary<string, string> metadata,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            _entries[key] = new RegistryMetadataEntry(metadata);
        }

        return ValueTask.CompletedTask;
    }

    public ValueTask<IRegistryMetadataEntry> GetRequiredAsync(
        string key,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!_entries.TryGetValue(key, out var entry))
        {
            throw new InvalidOperationException($"Registry metadata '{key}' was not found.");
        }

        return ValueTask.FromResult<IRegistryMetadataEntry>(entry);
    }

    public ValueTask DeleteIfAsync(
        string key,
        IReadOnlyDictionary<string, string> expected,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (_entries.TryGetValue(key, out var current) && current.ContainsAll(expected))
            {
                _entries.TryRemove(key, out _);
            }
        }

        return ValueTask.CompletedTask;
    }
}
