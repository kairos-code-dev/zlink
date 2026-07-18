using System.Collections.Concurrent;
using Zlink.Framework.Contracts.Eventing;

namespace LocationMessaging.Server.Consumer;

internal sealed class ConnectionEvidence
{
    private readonly ConcurrentQueue<string> _entries = new();
    // A pulse completed on every Add and swapped for a fresh one, so EVERY
    // concurrent waiter wakes — a counted semaphore hands one release to one
    // waiter and silently starves the rest.
    private readonly object _pulseGate = new();
    private TaskCompletionSource<bool> _pulse =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    public void Add(string entry)
    {
        _entries.Enqueue(entry);
        TaskCompletionSource<bool> pulse;
        lock (_pulseGate)
        {
            pulse = _pulse;
            _pulse = new TaskCompletionSource<bool>(
                TaskCreationOptions.RunContinuationsAsynchronously);
        }

        pulse.TrySetResult(true);
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
            Task pulseTask;
            lock (_pulseGate)
            {
                pulseTask = _pulse.Task;
            }

            var snapshot = _entries.ToArray();
            if (snapshot.Skip(Math.Clamp(afterCount, 0, snapshot.Length))
                .Any(line => line.Contains(contains, StringComparison.Ordinal))) return snapshot;

            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero
                || await Task.WhenAny(pulseTask, Task.Delay(remaining, cancellationToken)) != pulseTask)
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
