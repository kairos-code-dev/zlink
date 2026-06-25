using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelRuntimeBundle : IAsyncDisposable
{
    private readonly ZLinkSortedConnectionSet _manualConnections = new();

    public ZLinkChannelRuntimeBundle(
        IZLinkBackendSocket socket,
        ZLinkAsyncSubmitter? submitter = null,
        RoutingId localRid = default,
        string? socketRole = null)
    {
        Socket = socket;
        Submitter = submitter;
        LocalRid = localRid.Size > 0 ? localRid.ToString() : null;
        SocketRole = socketRole;
    }

    public IZLinkBackendSocket Socket { get; }

    public ZLinkAsyncSubmitter? Submitter { get; }

    public string? LocalRid { get; }

    public string? SocketRole { get; }

    public SemaphoreSlim ReceiveGate { get; } = new(1, 1);

    public IZLinkBackendDiscovery? Discovery { get; set; }

    public IZLinkBackendSpotRouteBridge? SpotRouteBridge { get; set; }

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

        if (SpotRouteBridge is not null)
        {
            await SpotRouteBridge.DisposeAsync();
        }

        await Socket.DisposeAsync();
        ReceiveGate.Dispose();
    }
}
