namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPeerConnectionSet
{
    private readonly object _gate = new();
    private readonly HashSet<string> _routerAuto = new(StringComparer.Ordinal);
    private readonly HashSet<string> _routerManual = new(StringComparer.Ordinal);
    private readonly Dictionary<string, RoutingId> _retainedManualPeerRids =
        new(StringComparer.Ordinal);

    public bool TryAddPeerManual(string endpoint)
    {
        lock (_gate)
        {
            return Acquire(_routerManual, endpoint);
        }
    }

    public bool TryAddPeerAuto(string endpoint)
    {
        lock (_gate) return Acquire(_routerAuto, endpoint);
    }

    public bool RemovePeerManual(string endpoint)
    {
        lock (_gate) return Release(_routerManual, endpoint);
    }

    public bool RemovePeerAuto(string endpoint)
    {
        lock (_gate) return Release(_routerAuto, endpoint);
    }

    public void RollbackPeerManual(string endpoint) { lock (_gate) _routerManual.Remove(endpoint); }
    public void RollbackPeerAuto(string endpoint) { lock (_gate) _routerAuto.Remove(endpoint); }

    public void RetainManualPeerRid(string endpoint, RoutingId peerRid)
    {
        lock (_gate) _retainedManualPeerRids[endpoint] = peerRid;
    }

    public bool HasRetainedManualPeer(RoutingId peerRid)
    {
        lock (_gate) return _retainedManualPeerRids.Values.Contains(peerRid);
    }

    private bool Acquire(HashSet<string> source, string endpoint)
    {
        var alreadyOwned = IsOwned(endpoint);
        return source.Add(endpoint) && !alreadyOwned;
    }

    private bool Release(HashSet<string> source, string endpoint)
    {
        if (!source.Remove(endpoint)) return false;
        return !IsOwned(endpoint);
    }

    private bool IsOwned(string endpoint)
        => _routerManual.Contains(endpoint)
           || _routerAuto.Contains(endpoint);

}
