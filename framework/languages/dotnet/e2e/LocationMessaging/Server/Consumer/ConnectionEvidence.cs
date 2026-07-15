using System.Collections.Concurrent;
using Zlink.Framework.Contracts.Eventing;

namespace LocationMessaging.Server.Consumer;

internal sealed class ConnectionEvidence
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly SemaphoreSlim _signal = new(0);

    public void Add(string entry)
    {
        _entries.Enqueue(entry);
        _signal.Release();
    }

    public async Task<string[]> WaitAsync(
        string contains,
        int afterCount,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (true)
        {
            var snapshot = _entries.ToArray();
            if (snapshot.Skip(Math.Clamp(afterCount, 0, snapshot.Length))
                .Any(line => line.Contains(contains, StringComparison.Ordinal))) return snapshot;

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero
                || !await _signal.WaitAsync(remaining, cancellationToken))
                throw new TimeoutException(
                    $"Connection evidence containing '{contains}' did not arrive within {timeout}.");
        }
    }
}

internal sealed class ConnectionEventObserver(
    ConnectionEvidence evidence,
    ILogger<ConnectionEventObserver> logger)
    : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var entry =
            $"monitor-socket|source={@event.SourceName}|kind={@event.Event}"
            + $"|remote={@event.RemoteAddr}|routing={@event.RoutingId}"
            + $"|value={@event.Diagnostic?.NativeValue}";
        evidence.Add(entry);
        logger.LogInformation("location connection evidence: {Entry}", entry);
        return ValueTask.CompletedTask;
    }
}
