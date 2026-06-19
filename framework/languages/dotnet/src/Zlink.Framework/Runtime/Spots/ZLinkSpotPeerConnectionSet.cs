namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPeerConnectionSet
{
    private readonly object _gate = new();
    private readonly ZLinkSortedConnectionSet _routerManual = new();
    private readonly ZLinkSortedConnectionSet _pubSubManual = new();
    private readonly HashSet<string> _routerDiscovered = new(StringComparer.Ordinal);

    public bool TryAddRouterManual(string endpoint)
    {
        lock (_gate)
        {
            return _routerManual.Add(endpoint);
        }
    }

    public bool TryAddPubSubManual(string endpoint)
    {
        lock (_gate)
        {
            return _pubSubManual.Add(endpoint);
        }
    }

    public bool TryAddRouterDiscovered(string endpoint)
    {
        lock (_gate)
        {
            return !_routerManual.Contains(endpoint)
                && _routerDiscovered.Add(endpoint);
        }
    }

    public void RemoveRouterManual(string endpoint)
    {
        lock (_gate)
        {
            _routerManual.Remove(endpoint);
            _routerDiscovered.Remove(endpoint);
        }
    }

    public void RemovePubSub(string endpoint)
    {
        lock (_gate)
        {
            _pubSubManual.Remove(endpoint);
        }
    }

    public IReadOnlyList<string> ListRouterManual()
    {
        lock (_gate)
        {
            return _routerManual.Snapshot();
        }
    }

    public IReadOnlyList<string> ListPubSubManual()
    {
        lock (_gate)
        {
            return _pubSubManual.Snapshot();
        }
    }
}
