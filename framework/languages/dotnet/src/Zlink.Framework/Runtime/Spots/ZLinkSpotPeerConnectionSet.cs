namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPeerConnectionSet
{
    private readonly object _gate = new();
    private readonly ZLinkSortedConnectionSet _routerManual = new();
    private readonly ZLinkSortedConnectionSet _pubSubManual = new();
    private readonly HashSet<string> _routerDiscovered = new(StringComparer.Ordinal);
    private readonly Dictionary<string, HashSet<string>> _routerDiscoveredRidKeys =
        new(StringComparer.Ordinal);

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

    public bool TryAddRouterDiscovered(string endpoint, RoutingId peerRid = default)
    {
        lock (_gate)
        {
            if (_routerManual.Contains(endpoint))
            {
                return false;
            }

            var addedEndpoint = _routerDiscovered.Add(endpoint);
            if (peerRid.Size <= 0)
            {
                return addedEndpoint;
            }

            if (!_routerDiscoveredRidKeys.TryGetValue(endpoint, out var ridKeys))
            {
                ridKeys = new HashSet<string>(StringComparer.Ordinal);
                _routerDiscoveredRidKeys[endpoint] = ridKeys;
            }

            return addedEndpoint || ridKeys.Add(peerRid.ToHex());
        }
    }

    public void RemoveRouterManual(string endpoint)
    {
        lock (_gate)
        {
            _routerManual.Remove(endpoint);
            _routerDiscovered.Remove(endpoint);
            _routerDiscoveredRidKeys.Remove(endpoint);
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
