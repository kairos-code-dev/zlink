using System.Collections.Concurrent;
using Zlink.Framework.Contracts.Configuration;
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

internal sealed class MeshConnectionEventObserver(
    ConnectionEvidence evidence,
    IZLinkRouteMeshRuntime meshRuntime,
    ILogger<MeshConnectionEventObserver> logger)
    : IZLinkRuntimeEventHandler<ZLinkMeshRuntimeEvent>
{
    private static readonly ConcurrentDictionary<string, string> LastKnownEndpoints =
        new(StringComparer.Ordinal);

    public ValueTask HandleAsync(ZLinkMeshRuntimeEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var peer = @event.PeerRid?.ToString() ?? string.Empty;
        var endpoint = string.Empty;
        if (peer.Length > 0)
        {
            try
            {
                endpoint = meshRuntime.Snapshot(@event.MeshName).Peers
                    .FirstOrDefault(candidate => candidate.Rid.ToString() == peer)?.Endpoint
                    ?? string.Empty;
            }
            catch (Exception)
            {
                // Shutdown can close the mesh before the final event is
                // dispatched. The last ready snapshot still names the peer.
            }

            if (endpoint.Length > 0) LastKnownEndpoints[peer] = endpoint;
            else LastKnownEndpoints.TryGetValue(peer, out endpoint!);
        }

        var kind = @event.Reason switch
        {
            "ready" => "ConnectionReady",
            "disconnected" => "Disconnected",
            _ => @event.Reason ?? @event.Identifier
        };
        var entry =
            $"monitor-mesh|source={@event.MeshName}|kind={kind}"
            + $"|remote={endpoint}|routing={peer}|sequence={@event.Sequence}";
        evidence.Add(entry);
        logger.LogInformation("location connection evidence: {Entry}", entry);
        return ValueTask.CompletedTask;
    }
}
