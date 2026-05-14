namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotDiscoveryReconciler(
    string spotChannelName,
    IZLinkBackendSpotNode node,
    ZLinkSpotPeerConnectionSet peerConnections,
    Func<IZLinkBackendDiscovery?> discoveryProvider)
{
    public void ConnectDiscoveredPubSubPeers()
    {
        var discovery = discoveryProvider();
        if (discovery is null)
        {
            return;
        }

        var localEndpoint = node.StatusSnapshot().LocalEndpoint;
        var connectedEndpoints = node.PeersSnapshot()
            .Select(static peer => peer.PeerEndpoint)
            .ToHashSet(StringComparer.Ordinal);
        foreach (var peer in discovery.MemberPeers())
        {
            if (peer.AutoConnectType != ZLinkAutoConnectType.SpotMesh
                || peer.ServiceRole != ZLinkServiceRole.Spot
                || !string.Equals(peer.ChannelName, spotChannelName, StringComparison.Ordinal)
                || string.IsNullOrWhiteSpace(peer.Endpoint)
                || string.Equals(peer.Endpoint, localEndpoint, StringComparison.Ordinal)
                || connectedEndpoints.Contains(peer.Endpoint))
            {
                continue;
            }

            if (peerConnections.TryAddPubSubDiscovered(peer.Endpoint))
            {
                try
                {
                    node.ConnectPeer(peer.Endpoint);
                }
                catch (ZlinkConnectException error)
                    when (error.InternalErrno == 16)
                {
                    // Discovery may connect the same peer between snapshots.
                }
            }
        }
    }
}
