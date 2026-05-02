namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendDiscoveryWrapper(Discovery nativeDiscovery) : IZLinkBackendDiscovery
{
    public object NativeInstance => nativeDiscovery;

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

    public ValueTask DisposeAsync() => nativeDiscovery.DisposeAsync();
}
