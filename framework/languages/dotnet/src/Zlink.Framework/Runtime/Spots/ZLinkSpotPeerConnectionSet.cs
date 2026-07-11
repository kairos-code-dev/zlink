namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPeerConnectionSet
{
    private readonly object _gate = new();
    private readonly HashSet<string> _pubSubAuto = new(StringComparer.Ordinal);
    private readonly HashSet<string> _pubSubManual = new(StringComparer.Ordinal);
    private readonly HashSet<string> _routerAuto = new(StringComparer.Ordinal);
    private readonly HashSet<string> _routerManual = new(StringComparer.Ordinal);

    public bool TryAddRouterManual(string endpoint)
    {
        lock (_gate)
        {
            return Acquire(_routerManual, endpoint);
        }
    }

    public bool TryAddPubSubManual(string endpoint)
    {
        lock (_gate)
        {
            return Acquire(_pubSubManual, endpoint);
        }
    }

    public bool TryAddRouterAuto(string endpoint)
    {
        lock (_gate) return Acquire(_routerAuto, endpoint);
    }

    public bool TryAddPubSubAuto(string endpoint)
    {
        lock (_gate) return Acquire(_pubSubAuto, endpoint);
    }

    public bool RemoveRouterManual(string endpoint)
    {
        lock (_gate) return Release(_routerManual, endpoint);
    }

    public bool RemovePubSubManual(string endpoint)
    {
        lock (_gate) return Release(_pubSubManual, endpoint);
    }

    public bool RemoveRouterAuto(string endpoint)
    {
        lock (_gate) return Release(_routerAuto, endpoint);
    }

    public bool RemovePubSubAuto(string endpoint)
    {
        lock (_gate) return Release(_pubSubAuto, endpoint);
    }

    public void RollbackRouterManual(string endpoint) { lock (_gate) _routerManual.Remove(endpoint); }
    public void RollbackPubSubManual(string endpoint) { lock (_gate) _pubSubManual.Remove(endpoint); }
    public void RollbackRouterAuto(string endpoint) { lock (_gate) _routerAuto.Remove(endpoint); }
    public void RollbackPubSubAuto(string endpoint) { lock (_gate) _pubSubAuto.Remove(endpoint); }

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
           || _routerAuto.Contains(endpoint)
           || _pubSubManual.Contains(endpoint)
           || _pubSubAuto.Contains(endpoint);

}
