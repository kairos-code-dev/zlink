namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotDiscoveryReconciler(
    string spotChannelName,
    IZLinkBackendSpotNode node,
    ZLinkSpotPeerConnectionSet peerConnections,
    Func<IZLinkBackendDiscovery?> discoveryProvider,
    bool routerEnabled,
    bool pubSubEnabled)
{
    public void ConnectDiscoveredPubSubPeers()
    {
        var discovery = discoveryProvider();
        if (discovery is null)
        {
            return;
        }

        var localEndpoint = node.Status().LocalEndpoint;
        var peers = discovery.MemberPeers();
        Debug(
            $"scan channel={spotChannelName} local={localEndpoint} peers={peers.Count} router={routerEnabled} pubsub={pubSubEnabled}");
        var routerEndpointsByRid = ResolveRouterEndpointsByRid(peers, spotChannelName);
        foreach (var peer in peers)
        {
            if (peer.AutoConnectType != ZLinkAutoConnectType.SpotMesh
                || !IsConnectablePeerRole(peer.ServiceRole)
                || !string.Equals(peer.ChannelName, spotChannelName, StringComparison.Ordinal)
                || string.IsNullOrWhiteSpace(peer.Endpoint)
                || string.Equals(peer.Endpoint, localEndpoint, StringComparison.Ordinal))
            {
                continue;
            }

            if (!routerEnabled)
            {
                continue;
            }

            var routerEndpoint = ResolveRouterEndpoint(
                discovery,
                peer,
                routerEndpointsByRid,
                out var peerRoutingId);
            if (string.IsNullOrWhiteSpace(routerEndpoint)
                || string.Equals(routerEndpoint, localEndpoint, StringComparison.Ordinal)
                || !peerConnections.TryAddRouterDiscovered(routerEndpoint, peerRoutingId))
            {
                Debug(
                    $"skip peer={peer.Endpoint} rid={peer.RoutingId?.ToHex() ?? "-"} routerEndpoint={routerEndpoint}");
                continue;
            }

            Debug(
                $"connect peer={peer.Endpoint} rid={peerRoutingId.ToHex()} routerEndpoint={routerEndpoint}");
            ConnectRouterPeer(peerRoutingId, routerEndpoint);
        }
    }

    private string ResolveRouterEndpoint(
        IZLinkBackendDiscovery discovery,
        ZLinkMemberPeerEntry peer,
        IReadOnlyDictionary<RoutingId, string> routerEndpointsByRid,
        out RoutingId peerRoutingId)
    {
        peerRoutingId = default;
        if (peer.RoutingId is not { Size: > 0 } resolvedPeerRoutingId)
        {
            return pubSubEnabled ? string.Empty : peer.Endpoint;
        }

        peerRoutingId = resolvedPeerRoutingId;
        if (routerEndpointsByRid.TryGetValue(resolvedPeerRoutingId, out var discoveredEndpoint))
        {
            return discoveredEndpoint;
        }

        if (ZLinkSpotRouterEndpointDiscovery.TryResolveEndpoint(
                discovery,
                resolvedPeerRoutingId,
                out var routerEndpoint))
        {
            return routerEndpoint;
        }

        return pubSubEnabled ? string.Empty : peer.Endpoint;
    }

    private static IReadOnlyDictionary<RoutingId, string> ResolveRouterEndpointsByRid(
        IReadOnlyList<ZLinkMemberPeerEntry> peers,
        string spotChannelName)
    {
        Dictionary<RoutingId, string>? endpoints = null;
        foreach (var peer in peers)
        {
            if (peer.AutoConnectType != ZLinkAutoConnectType.SpotMesh
                || peer.ServiceRole != ZLinkServiceRole.Router
                || !string.Equals(peer.ChannelName, spotChannelName, StringComparison.Ordinal)
                || string.IsNullOrWhiteSpace(peer.Endpoint)
                || peer.RoutingId is not { Size: > 0 } routingId)
            {
                continue;
            }

            endpoints ??= [];
            endpoints[routingId] = peer.Endpoint;
        }

        return endpoints ?? EmptyRouterEndpointsByRid;
    }

    private bool IsConnectablePeerRole(ZLinkServiceRole role)
    {
        if (pubSubEnabled)
        {
            return role == ZLinkServiceRole.Spot;
        }

        return routerEnabled && role == ZLinkServiceRole.Router;
    }

    private static readonly IReadOnlyDictionary<RoutingId, string> EmptyRouterEndpointsByRid =
        new Dictionary<RoutingId, string>();

    private static void Debug(string message)
    {
        ZLinkFrameworkDebugLog.SpotDiscovery(message);
    }

    private void ConnectRouterPeer(RoutingId peerRoutingId, string endpoint)
    {
        try
        {
            if (peerRoutingId.Size > 0)
            {
                node.ConnectPeer(peerRoutingId, endpoint);
                return;
            }

            node.ConnectPeer(endpoint);
        }
        catch (ZlinkConnectException error)
            when (error.NativeErrno == 16)
        {
            // Discovery may connect the same peer between snapshots.
        }
    }
}
