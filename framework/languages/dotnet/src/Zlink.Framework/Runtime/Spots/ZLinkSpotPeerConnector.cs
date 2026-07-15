namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPeerConnector(
    IZLinkBackendSpotNode node,
    ZLinkSpotPeerConnectionSet connections)
{
    private readonly object _gate = new();

    public ValueTask<bool> ConnectRouterAsync(string endpoint, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!connections.TryAddRouterManual(endpoint)) return ValueTask.FromResult(false);
            try { ConnectPeer(endpoint); }
            catch { connections.RollbackRouterManual(endpoint); throw; }
            return ValueTask.FromResult(true);
        }
    }

    public ValueTask<bool> ConnectRouterAsync(
        RoutingId peerRid,
        string endpoint,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!connections.TryAddRouterManual(endpoint)) return ValueTask.FromResult(false);
            try { ConnectPeer(peerRid, endpoint); }
            catch { connections.RollbackRouterManual(endpoint); throw; }
            return ValueTask.FromResult(true);
        }
    }

    public ValueTask<bool> ConnectPubSubAsync(string endpoint, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!connections.TryAddPubSubManual(endpoint)) return ValueTask.FromResult(false);
            try { ConnectPeer(endpoint); }
            catch { connections.RollbackPubSubManual(endpoint); throw; }
            return ValueTask.FromResult(true);
        }
    }

    public void Disconnect(string endpoint)
    {
        DisconnectRouterManual(endpoint);
        DisconnectPubSubManual(endpoint);
    }

    public void DisconnectRouterManual(string endpoint)
    {
        lock (_gate)
        {
            if (!connections.RemoveRouterManual(endpoint)) return;
            try { node.DisconnectPeer(endpoint); }
            catch { _ = connections.TryAddRouterManual(endpoint); throw; }
        }
    }

    public void DisconnectPubSubManual(string endpoint)
    {
        lock (_gate)
        {
            if (!connections.RemovePubSubManual(endpoint)) return;
            try { node.DisconnectPeer(endpoint); }
            catch { _ = connections.TryAddPubSubManual(endpoint); throw; }
        }
    }

    public bool ConnectRouterAuto(RoutingId? peerRid, string endpoint)
    {
        lock (_gate)
        {
            return ConnectAuto(
                endpoint,
                connections.TryAddRouterAuto,
                connections.RollbackRouterAuto,
                () =>
                {
                    if (peerRid is { Size: > 0 } rid) ConnectPeer(rid, endpoint);
                    else ConnectPeer(endpoint);
                });
        }
    }

    public bool ConnectPubSubAuto(string endpoint)
    {
        lock (_gate)
        {
            return ConnectAuto(
                endpoint,
                connections.TryAddPubSubAuto,
                connections.RollbackPubSubAuto,
                () => ConnectPeer(endpoint));
        }
    }

    public bool DisconnectRouterAuto(string endpoint)
    {
        lock (_gate)
        {
            return DisconnectAuto(endpoint, connections.RemoveRouterAuto, connections.TryAddRouterAuto);
        }
    }

    public bool DisconnectPubSubAuto(string endpoint)
    {
        lock (_gate)
        {
            return DisconnectAuto(endpoint, connections.RemovePubSubAuto, connections.TryAddPubSubAuto);
        }
    }

    private static bool ConnectAuto(
        string endpoint,
        Func<string, bool> acquire,
        Action<string> rollback,
        Action connect)
    {
        if (!acquire(endpoint)) return true;
        try
        {
            connect();
            return true;
        }
        catch
        {
            rollback(endpoint);
            return false;
        }
    }

    private bool DisconnectAuto(
        string endpoint,
        Func<string, bool> release,
        Func<string, bool> restore)
    {
        if (!release(endpoint)) return true;
        try
        {
            node.DisconnectPeer(endpoint);
            return true;
        }
        catch
        {
            _ = restore(endpoint);
            return false;
        }
    }

    private void ConnectPeer(string endpoint)
    {
        node.ConnectPeer(endpoint);
    }

    private void ConnectPeer(RoutingId peerRid, string endpoint)
    {
        node.ConnectPeer(peerRid, endpoint);
    }
}
