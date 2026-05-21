namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendDiscoveryWrapper(Discovery nativeDiscovery) : IZLinkBackendDiscovery
{
    public object NativeInstance => nativeDiscovery;

    public bool SpotOwnerSyncEnabled
    {
        get => nativeDiscovery.SpotOwnerSyncEnabled;
        set => nativeDiscovery.SpotOwnerSyncEnabled = value;
    }

    public bool ActorRouteSyncEnabled
    {
        get => nativeDiscovery.ActorRouteSyncEnabled;
        set => nativeDiscovery.ActorRouteSyncEnabled = value;
    }

    public void ConnectRegistry(string endpoint)
    {
        nativeDiscovery.ConnectRegistry(endpoint);
    }

    public IReadOnlyList<ZLinkMemberPeerEntry> MemberPeers()
    {
        return nativeDiscovery.MemberPeers()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public SpotRoute ResolveSpot(RoutingId spotRid)
    {
        return nativeDiscovery.ResolveSpot(spotRid);
    }

    public ActorRoute ResolveActor(string actorId)
    {
        return nativeDiscovery.ResolveActor(actorId);
    }

    public void BindRoute(uint kind, ReadOnlySpan<byte> key, ReadOnlySpan<byte> value)
    {
        nativeDiscovery.BindRoute(kind, key, value);
    }

    public void UnbindRoute(uint kind, ReadOnlySpan<byte> key)
    {
        nativeDiscovery.UnbindRoute(kind, key);
    }

    public ZLinkBackendDiscoveryRoute ResolveRoute(uint kind, ReadOnlySpan<byte> key)
    {
        var route = nativeDiscovery.ResolveRoute(kind, key);
        return new ZLinkBackendDiscoveryRoute(route.OwnerRoutingId, route.Value);
    }

    public ValueTask DisposeAsync() => nativeDiscovery.DisposeAsync();
}
