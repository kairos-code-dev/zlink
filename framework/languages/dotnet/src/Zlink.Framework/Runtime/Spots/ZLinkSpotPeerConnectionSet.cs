namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPeerConnectionSet
{
    private readonly object _gate = new();
    private readonly ZLinkSortedConnectionSet _pubSubManual = new();
    private readonly ZLinkSortedConnectionSet _routerManual = new();

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

}
