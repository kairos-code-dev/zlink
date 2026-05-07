using ZlinkRegistry = Systems.Zlink.Registry;

namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendRegistryWrapper(ZlinkRegistry nativeRegistry) : IZLinkBackendRegistry
{
    public object NativeInstance => nativeRegistry;

    public void SetId(uint registryId)
    {
        nativeRegistry.SetId(registryId);
    }

    public void SetHeartbeat(uint intervalMs, uint timeoutMs)
    {
        nativeRegistry.SetHeartbeat(intervalMs, timeoutMs);
    }

    public void SetBroadcastInterval(uint intervalMs)
    {
        nativeRegistry.SetBroadcastInterval(intervalMs);
    }

    public void AddPeer(string endpoint)
    {
        nativeRegistry.AddPeer(endpoint);
    }

    public void Bind(string pubEndpoint, string routerEndpoint)
    {
        nativeRegistry.Bind(pubEndpoint, routerEndpoint);
    }

    public ZLinkRegistryStatus StatusSnapshot()
    {
        return nativeRegistry.StatusSnapshot().ToFramework();
    }

    public IReadOnlyList<ZLinkRegistryServiceSummaryEntry> ServiceSummarySnapshot(
        ZLinkRegistryServiceSummaryFilter? filter)
    {
        return nativeRegistry.ServiceSummarySnapshot(filter.ToNative())
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkRegistryTopologyEntry> TopologySnapshot()
    {
        return nativeRegistry.TopologySnapshot()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkRegistryTopologyEntry> TopologyQuery(
        ZLinkRegistryTopologyFilter? filter)
    {
        return nativeRegistry.TopologyQuery(filter.ToNative())
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
