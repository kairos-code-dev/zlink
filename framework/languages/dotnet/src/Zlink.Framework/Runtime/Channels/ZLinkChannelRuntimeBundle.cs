using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRuntimeBundle : IAsyncDisposable
{
    private readonly ZLinkSortedConnectionSet _manualConnections = new();

    public ZLinkChannelRuntimeBundle(
        IZLinkBackendSocket socket,
        ZLinkAsyncSubmitter? submitter = null)
    {
        Socket = socket;
        Submitter = submitter;
    }

    public IZLinkBackendSocket Socket { get; }

    public ZLinkAsyncSubmitter? Submitter { get; }

    public SemaphoreSlim ReceiveGate { get; } = new(1, 1);

    public ZLinkDealerMeshPendingRequests DealerMeshPendingRequests { get; } = new();

    public IZLinkBackendDiscovery? Discovery { get; set; }

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

    public async ValueTask DisposeAsync()
    {
        if (Submitter is not null)
        {
            await Submitter.DisposeAsync();
        }

        if (Discovery is not null)
        {
            await Discovery.DisposeAsync();
        }

        await Socket.DisposeAsync();
        ReceiveGate.Dispose();
    }
}
