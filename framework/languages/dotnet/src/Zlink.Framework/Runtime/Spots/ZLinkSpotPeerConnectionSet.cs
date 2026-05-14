namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPeerConnectionSet
{
    private readonly object _gate = new();
    private readonly ZLinkSortedConnectionSet _routerManual = new();
    private readonly ZLinkSortedConnectionSet _pubSubManual = new();
    private readonly HashSet<string> _pubSubDiscovered = new(StringComparer.Ordinal);

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

    public bool TryAddPubSubDiscovered(string endpoint)
    {
        lock (_gate)
        {
            return !_pubSubManual.Contains(endpoint)
                && _pubSubDiscovered.Add(endpoint);
        }
    }

    public void RemoveRouterManual(string endpoint)
    {
        lock (_gate)
        {
            _routerManual.Remove(endpoint);
        }
    }

    public void RemovePubSub(string endpoint)
    {
        lock (_gate)
        {
            _pubSubManual.Remove(endpoint);
            _pubSubDiscovered.Remove(endpoint);
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
