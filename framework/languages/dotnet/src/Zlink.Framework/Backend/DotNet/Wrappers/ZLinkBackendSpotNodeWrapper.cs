namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendSpotNodeWrapper(global::Zlink.SpotNode nativeSpotNode) : IZLinkBackendSpotNode
{
    public object NativeInstance => nativeSpotNode;

    public global::Zlink.RoutingId RoutingId => nativeSpotNode.RoutingId;

    public void Bind(string endpoint)
    {
        nativeSpotNode.Bind(endpoint);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSpotNode.AttachDiscovery(discovery.RequireNative<global::Zlink.Discovery>());
    }

    public void ConnectPeer(string endpoint)
    {
        nativeSpotNode.ConnectPeer(endpoint);
    }

    public void DisconnectPeer(string endpoint)
    {
        nativeSpotNode.DisconnectPeer(endpoint);
    }

    public IZLinkBackendSpot CreateSpot()
    {
        return new ZLinkBackendSpotWrapper(nativeSpotNode.CreateSpot());
    }

    public ZLinkSpotNodeStatus StatusSnapshot()
    {
        return nativeSpotNode.StatusSnapshot().ToFramework();
    }

    public IReadOnlyList<ZLinkSpotNodePeerEntry> PeersSnapshot()
    {
        return nativeSpotNode.PeersSnapshot()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public IReadOnlyList<ZLinkSpotNodeSubjectEntry> SubjectsSnapshot()
    {
        return nativeSpotNode.SubjectsSnapshot()
            .Select(static entry => entry.ToFramework())
            .ToArray();
    }

    public void AttachChannelDealer(IZLinkBackendDiscovery discovery, IZLinkBackendDealerSocket dealer)
    {
        nativeSpotNode.AttachChannelDealer(
            discovery.RequireNative<global::Zlink.Discovery>(),
            dealer.RequireNative<global::Zlink.DealerSocket>());
    }

    public void AttachChannelDealerManual(string channelName, IZLinkBackendDealerSocket dealer)
    {
        nativeSpotNode.AttachChannelDealerManual(
            channelName,
            dealer.RequireNative<global::Zlink.DealerSocket>());
    }

    public ValueTask DisposeAsync() => nativeSpotNode.DisposeAsync();
}
