namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPeerConnector(
    IZLinkBackendSpotNode node,
    ZLinkSpotPeerConnectionSet connections)
{
    private readonly object _gate = new();

    public ValueTask<bool> ConnectPeerAsync(string endpoint, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!connections.TryAddPeerManual(endpoint)) return ValueTask.FromResult(false);
            try { ConnectPeer(endpoint); }
            catch { connections.RollbackPeerManual(endpoint); throw; }
            return ValueTask.FromResult(true);
        }
    }

    public ValueTask<bool> ConnectPeerAsync(
        RoutingId peerRid,
        string endpoint,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (!connections.TryAddPeerManual(endpoint)) return ValueTask.FromResult(false);
            try { ConnectPeer(peerRid, endpoint, "none"); }
            catch { connections.RollbackPeerManual(endpoint); throw; }
            return ValueTask.FromResult(true);
        }
    }

    public void Disconnect(string endpoint)
    {
        DisconnectPeerManual(endpoint);
    }

    public void DisconnectPeerManual(string endpoint)
    {
        lock (_gate)
        {
            if (!connections.RemovePeerManual(endpoint)) return;
            try { node.DisconnectPeer(endpoint); }
            catch { _ = connections.TryAddPeerManual(endpoint); throw; }
        }
    }

    public bool ConnectPeerAuto(
        RoutingId? peerRid,
        string endpoint,
        string expectedSecurityIdentity)
    {
        lock (_gate)
        {
            return ConnectAuto(
                endpoint,
                connections.TryAddPeerAuto,
                connections.RollbackPeerAuto,
                () =>
                {
                    if (peerRid is { Size: > 0 } rid)
                        ConnectPeer(rid, endpoint, expectedSecurityIdentity);
                    else ConnectPeer(endpoint);
                });
        }
    }

    public bool DisconnectPeerAuto(string endpoint)
    {
        lock (_gate)
        {
            return DisconnectAuto(endpoint, connections.RemovePeerAuto, connections.TryAddPeerAuto);
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

    private void ConnectPeer(
        RoutingId peerRid,
        string endpoint,
        string expectedSecurityIdentity)
    {
        node.ConnectPeer(peerRid, endpoint, expectedSecurityIdentity);
    }
}
