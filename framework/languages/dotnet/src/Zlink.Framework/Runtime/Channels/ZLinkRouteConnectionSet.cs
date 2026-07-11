namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteConnectionSet(IZLinkBackendRouterSocket router)
{
    private readonly HashSet<string> _autoConnections = new(StringComparer.Ordinal);
    private readonly HashSet<string> _manualConnections = new(StringComparer.Ordinal);
    private readonly object _connectGate = new();

    public void ConnectManual(string endpoint)
    {
        lock (_connectGate)
        {
            Acquire(_manualConnections, endpoint, () => router.Connect(endpoint));
        }
    }

    /// <summary>
    /// Connects to a peer whose routing id is already known (from its peer
    /// location row). The connect routing id must be assigned before the
    /// dial or rid-addressed sends toward this peer fail with host
    /// unreachable; the gate keeps the option-and-connect pair atomic when
    /// several reconcile loops share the socket.
    /// </summary>
    public void ConnectManual(RoutingId peerRid, string endpoint)
    {
        lock (_connectGate)
        {
            Acquire(_manualConnections, endpoint, () => Connect(peerRid, endpoint));
        }
    }

    public void DisconnectManual(string endpoint)
    {
        lock (_connectGate)
        {
            Release(_manualConnections, endpoint, throwOnFailure: true);
        }
    }

    public bool ConnectAuto(RoutingId? peerRid, string endpoint)
    {
        lock (_connectGate)
        {
            return TryAcquire(
                _autoConnections,
                endpoint,
                () =>
                {
                    if (peerRid is { Size: > 0 } rid) Connect(rid, endpoint);
                    else router.Connect(endpoint);
                });
        }
    }

    public bool DisconnectAuto(string endpoint)
    {
        lock (_connectGate) return Release(_autoConnections, endpoint, throwOnFailure: false);
    }

    public IReadOnlyList<string> List()
    {
        lock (_connectGate)
            return _manualConnections.Concat(_autoConnections)
                .Distinct(StringComparer.Ordinal)
                .OrderBy(static endpoint => endpoint, StringComparer.Ordinal)
                .ToArray();
    }

    private void Acquire(HashSet<string> source, string endpoint, Action connect)
    {
        if (source.Contains(endpoint)) return;
        if (!TryAcquire(source, endpoint, connect))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RouteNotConnected,
                $"Failed to connect route endpoint '{endpoint}'.");
    }

    private bool TryAcquire(HashSet<string> source, string endpoint, Action connect)
    {
        var alreadyOwned = _manualConnections.Contains(endpoint) || _autoConnections.Contains(endpoint);
        if (!source.Add(endpoint)) return true;
        if (alreadyOwned) return true;
        try
        {
            connect();
            return true;
        }
        catch
        {
            source.Remove(endpoint);
            return false;
        }
    }

    private bool Release(HashSet<string> source, string endpoint, bool throwOnFailure)
    {
        if (!source.Remove(endpoint)) return true;
        if (_manualConnections.Contains(endpoint) || _autoConnections.Contains(endpoint)) return true;
        try
        {
            router.Disconnect(endpoint);
            return true;
        }
        catch when (!throwOnFailure)
        {
            source.Add(endpoint);
            return false;
        }
        catch
        {
            source.Add(endpoint);
            throw;
        }
    }

    private void Connect(RoutingId peerRid, string endpoint)
    {
        router.SetConnectRoutingId(peerRid);
        router.SetProbe(true);
        router.Connect(endpoint);
    }
}
