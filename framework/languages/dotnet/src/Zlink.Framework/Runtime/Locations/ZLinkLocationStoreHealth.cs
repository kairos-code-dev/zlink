namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Aggregates store read failures by operation boundary. A successful read
/// clears only its own failure, so one healthy subsystem cannot hide an
/// outage still affecting another location capability.
/// </summary>
internal sealed class ZLinkLocationStoreHealth
{
    private readonly object _gate = new();
    private readonly Dictionary<string, string> _failures = new(StringComparer.Ordinal);
    private DateTimeOffset? _lastSuccessAt;

    internal void ReportSuccess(string source)
    {
        lock (_gate)
        {
            _failures.Remove(source);
            _lastSuccessAt = DateTimeOffset.UtcNow;
        }
    }

    internal void ReportFailure(string source, Exception error)
    {
        lock (_gate) _failures[source] = error.Message;
    }

    internal Snapshot GetSnapshot()
    {
        lock (_gate)
        {
            return new Snapshot(
                _failures.Count == 0,
                _lastSuccessAt,
                _failures.Count == 0
                    ? null
                    : string.Join("; ", _failures.OrderBy(static pair => pair.Key)
                        .Select(static pair => $"{pair.Key}: {pair.Value}")));
        }
    }

    internal readonly record struct Snapshot(
        bool Healthy,
        DateTimeOffset? LastSuccessAt,
        string? LastError);
}

internal static class ZLinkLocationStoreRead
{
    internal static async ValueTask<T> ExecuteAsync<T>(
        ZLinkLocationStoreHealth? health,
        string source,
        CancellationToken callerToken,
        Func<ValueTask<T>> read)
    {
        try
        {
            var result = await read().ConfigureAwait(false);
            health?.ReportSuccess(source);
            return result;
        }
        catch (OperationCanceledException) when (callerToken.IsCancellationRequested)
        {
            throw;
        }
        catch (Exception error)
        {
            health?.ReportFailure(source, error);
            ZLinkRuntimeMetrics.RecordLocationStoreError();
            throw;
        }
    }
}
