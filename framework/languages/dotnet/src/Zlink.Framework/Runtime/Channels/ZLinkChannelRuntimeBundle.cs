using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRuntimeBundle : IAsyncDisposable
{
    private readonly ZLinkSortedConnectionSet _manualConnections = new();

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

    public IZLinkBackendSpotRouteBridge? SpotRouteBridge { get; set; }

    public async ValueTask DisposeAsync()
    {
        if (Submitter is not null) await Submitter.DisposeAsync();

        if (CompletionPump is not null) await CompletionPump.DisposeAsync();

        if (SpotRouteBridge is not null) await SpotRouteBridge.DisposeAsync();

        await Socket.DisposeAsync();
        ReceiveGate.Dispose();
    }

    public bool TryAddManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            return _manualConnections.Add(endpoint);
        }
    }

    public void RemoveManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            _manualConnections.Remove(endpoint);
        }
    }

    public bool ContainsManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            return _manualConnections.Contains(endpoint);
        }
    }

    public IReadOnlyList<string> ListManualConnections()
    {
        lock (_manualConnections)
        {
            return _manualConnections.Snapshot();
        }
    }
}
