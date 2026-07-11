using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRuntimeBundle : IAsyncDisposable
{
    private readonly ZLinkSortedConnectionSet _manualConnections = new();
    private int _disposed;

    public ZLinkChannelRuntimeBundle(
        IZLinkBackendSocket socket,
        ZLinkAsyncSubmitter? submitter = null,
        ZLinkRequestCompletionPump? completionPump = null,
        RoutingId localRid = default,
        string? socketRole = null)
    {
        Socket = socket;
        Submitter = submitter;
        CompletionPump = completionPump;
        LocalRid = localRid.Size > 0 ? localRid.ToString() : null;
        SocketRole = socketRole;
    }

    public IZLinkBackendSocket Socket { get; }

    public ZLinkAsyncSubmitter? Submitter { get; }

    public ZLinkRequestCompletionPump? CompletionPump { get; }

    public string? LocalRid { get; }

    public string? SocketRole { get; }

    public SemaphoreSlim ReceiveGate { get; } = new(1, 1);

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
        var failures = new ZLinkFailureCollector();
        if (Submitter is not null)
            await failures.CaptureAsync(Submitter.DisposeAsync).ConfigureAwait(false);

        if (CompletionPump is not null)
            await failures.CaptureAsync(CompletionPump.DisposeAsync).ConfigureAwait(false);

        await failures.CaptureAsync(Socket.DisposeAsync).ConfigureAwait(false);
        failures.Capture(ReceiveGate.Dispose);
        failures.ThrowIfAny();
    }

    public bool TryAddManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            return _manualConnections.Add(endpoint);
        }
    }

    public bool ContainsManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            return _manualConnections.Contains(endpoint);
        }
    }

}
