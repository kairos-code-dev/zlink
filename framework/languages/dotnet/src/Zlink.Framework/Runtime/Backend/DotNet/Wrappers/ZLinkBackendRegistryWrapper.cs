namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendRegistryWrapper(IRegistry nativeRegistry) : IZLinkBackendRegistry
{
    public object NativeInstance => nativeRegistry;

    public void SetId(uint registryId)
    {
        nativeRegistry.SetId(registryId);
    }

    public void SetHeartbeat(uint intervalMs, uint timeoutMs)
    {
        nativeRegistry.SetHeartbeat(
            TimeSpan.FromMilliseconds(intervalMs),
            TimeSpan.FromMilliseconds(timeoutMs));
    }

    public void SetBroadcastInterval(uint intervalMs)
    {
        nativeRegistry.SetBroadcastInterval(TimeSpan.FromMilliseconds(intervalMs));
    }

    public void AddPeer(string endpoint)
    {
        nativeRegistry.AddPeer(endpoint);
    }

    public void Bind(string pubEndpoint, string routerEndpoint)
    {
        nativeRegistry.Bind(pubEndpoint, routerEndpoint);
    }

    public ZLinkRegistryStatus Status()
    {
        return nativeRegistry.Status().ToFramework();
    }

    public IReadOnlyList<ZLinkRegistryServiceSummaryEntry> ServiceSummary(
        ZLinkRegistryServiceSummaryFilter? filter)
    {
        return nativeRegistry.ServiceSummary(filter.ToNative())
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkRegistryTopologyEntry> Topology()
    {
        return nativeRegistry.Topology()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkRegistryTopologyEntry> Topology(
        ZLinkRegistryTopologyFilter? filter)
    {
        return nativeRegistry.Topology(filter.ToNative())
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkMemberPeerEntry> MemberPeers(string channelName)
    {
        return nativeRegistry.MemberPeers(channelName)
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public ValueTask DisposeAsync() => nativeRegistry.DisposeAsync();
}
