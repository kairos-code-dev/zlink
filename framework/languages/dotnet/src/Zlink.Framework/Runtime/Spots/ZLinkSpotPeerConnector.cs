namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPeerConnector(
    IZLinkBackendSpotNode node,
    ZLinkSpotPeerConnectionSet connections)
{
    public ValueTask<bool> ConnectRouterAsync(string endpoint, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!connections.TryAddRouterManual(endpoint)) return ValueTask.FromResult(false);

        ConnectRouterPeer(endpoint);
        return ValueTask.FromResult(true);
    }

    public ValueTask<bool> ConnectRouterAsync(
        RoutingId peerRid,
        string endpoint,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!connections.TryAddRouterManual(endpoint)) return ValueTask.FromResult(false);

        ConnectRouterPeer(peerRid, endpoint);
        return ValueTask.FromResult(true);
    }

    public ValueTask<bool> ConnectPubSubAsync(string endpoint, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!connections.TryAddPubSubManual(endpoint)) return ValueTask.FromResult(false);

        ConnectPeer(endpoint);
        return ValueTask.FromResult(true);
    }

    private void ConnectPeer(string endpoint)
    {
        try
        {
            node.ConnectPeer(endpoint);
        }
        catch (ZlinkConnectException error)
            when (error.Result == ZlinkConnectException.ErrorCode.Busy)
        {
        }
    }

    private void ConnectRouterPeer(string endpoint)
    {
        try
        {
            node.ConnectPeer(endpoint);
        }
        catch (ZlinkConnectException error)
            when (error.Result == ZlinkConnectException.ErrorCode.Busy)
        {
        }
    }

    private void ConnectRouterPeer(RoutingId peerRid, string endpoint)
    {
        try
        {
            node.ConnectPeer(peerRid, endpoint);
        }
        catch (ZlinkConnectException error)
            when (error.Result == ZlinkConnectException.ErrorCode.Busy)
        {
        }
    }
}
