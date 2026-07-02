namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteConnectionSet(IZLinkBackendRouterSocket router)
{
    private readonly ZLinkSortedConnectionSet _manualConnections = new();
    private readonly object _connectGate = new();

    public void Connect(string endpoint)
    {
        if (_manualConnections.Add(endpoint)) router.Connect(endpoint);
    }

    /// <summary>
    /// Connects to a peer whose routing id is already known (from its peer
    /// location row). The connect routing id must be assigned before the
    /// dial or rid-addressed sends toward this peer fail with host
    /// unreachable; the gate keeps the option-and-connect pair atomic when
    /// several reconcile loops share the socket.
    /// </summary>
    public void Connect(RoutingId peerRid, string endpoint)
    {
        if (!_manualConnections.Add(endpoint))
        {
            return;
        }

        lock (_connectGate)
        {
            router.SetConnectRoutingId(peerRid);
            router.Connect(endpoint);
        }
    }

    public void Disconnect(string endpoint)
    {
        router.Disconnect(endpoint);
        _manualConnections.Remove(endpoint);
    }

    public IReadOnlyList<string> List()
    {
        return _manualConnections.Snapshot();
    }
}