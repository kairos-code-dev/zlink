namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// A local capability that participates in auto connect: what this node
/// advertises for one mesh and what it dials from.
/// </summary>
internal sealed record ZLinkAutoConnectLocal(
    ZLinkLocationAutoConnectType AutoConnectType,
    string MeshName,
    ZLinkLocationRole Role,
    RoutingId? NodeRid,
    string Endpoint);

/// <summary>
/// One connect target chosen by the planner. The key identifies the peer
/// (node rid + role when the rid exists, endpoint + role otherwise) so an
/// endpoint change under the same key is treated as a handover.
/// </summary>
internal sealed record ZLinkAutoConnectTarget(
    string TargetKey,
    RoutingId? NodeRid,
    ZLinkLocationRole Role,
    string Endpoint,
    string? ConnectionFingerprint = null,
    IReadOnlyDictionary<string, string>? Metadata = null,
    string? OwnerId = null);

/// <summary>
/// Pure desired-target-set computation: the role allow table and target
/// matching rules of the draft, with the pairwise initiator order for
/// symmetric meshes. No I/O, so every rule is unit-testable in isolation.
/// </summary>
internal static class ZLinkAutoConnectPlanner
{
    internal static bool IsRoleAllowed(
        ZLinkLocationAutoConnectType type,
        ZLinkLocationRole role) => type switch
    {
        ZLinkLocationAutoConnectType.RouteMesh => role == ZLinkLocationRole.Router,
        ZLinkLocationAutoConnectType.ClientServer =>
            role is ZLinkLocationRole.Router or ZLinkLocationRole.Dealer,
        ZLinkLocationAutoConnectType.DealerMesh => role == ZLinkLocationRole.Dealer,
        ZLinkLocationAutoConnectType.Fanout =>
            role is ZLinkLocationRole.Pub or ZLinkLocationRole.Sub,
        ZLinkLocationAutoConnectType.SpotMesh =>
            role is ZLinkLocationRole.Spot or ZLinkLocationRole.Router,
        _ => false
    };

    internal static IReadOnlyDictionary<string, ZLinkAutoConnectTarget> ComputeDesired(
        ZLinkAutoConnectLocal local,
        IReadOnlyList<ZLinkPeerLocation> peers)
    {
        var desired = new Dictionary<string, ZLinkAutoConnectTarget>(StringComparer.Ordinal);
        foreach (var peer in peers)
        {
            if (peer.AutoConnectType != local.AutoConnectType
                || !string.Equals(peer.MeshName, local.MeshName, StringComparison.Ordinal)
                || !IsRoleAllowed(local.AutoConnectType, peer.Role)
                || string.IsNullOrEmpty(peer.Endpoint))
            {
                continue;
            }

            if (IsSelf(local, peer) || !ShouldDial(local, peer))
            {
                continue;
            }

            var target = new ZLinkAutoConnectTarget(
                TargetKeyOf(peer), peer.NodeRid, peer.Role, peer.Endpoint,
                ConnectionFingerprintOf(peer), peer.Metadata,
                peer.OwnerId);
            desired[target.TargetKey] = target;
        }

        return desired;
    }

    private static string? ConnectionFingerprintOf(ZLinkPeerLocation peer)
    {
        if (peer.AutoConnectType != ZLinkLocationAutoConnectType.SpotMesh
            || peer.Metadata is null
            || !peer.Metadata.TryGetValue(
                ZLinkLocationAutoConnectHost.SpotPubEndpointMetadataKey,
                out var endpoint)) return null;
        return endpoint;
    }

    internal static string TargetKeyOf(ZLinkPeerLocation peer)
    {
        // Node rid + role identifies the peer when the rid exists; roles
        // without a rid fall back to endpoint + role. An endpoint change
        // under the same key is a peer handover, not a new peer.
        var identity = peer.NodeRid is { Size: > 0 } rid ? rid.ToHex() : peer.Endpoint;
        return $"{peer.Role.ToCanonicalString()}|{identity}";
    }

    private static bool IsSelf(ZLinkAutoConnectLocal local, ZLinkPeerLocation peer)
    {
        if (local.NodeRid is { Size: > 0 } localRid
            && peer.NodeRid is { Size: > 0 } peerRid
            && localRid.Equals(peerRid))
        {
            return true;
        }

        return string.Equals(peer.Endpoint, local.Endpoint, StringComparison.Ordinal);
    }

    private static bool ShouldDial(ZLinkAutoConnectLocal local, ZLinkPeerLocation peer) =>
        local.AutoConnectType switch
        {
            // Symmetric meshes use the pairwise initiator so a pair never
            // double-connects (draft 4/14.3): both sides dialing creates two
            // links for one routing id and breaks rid-addressed requests.
            ZLinkLocationAutoConnectType.RouteMesh =>
                local.Role == ZLinkLocationRole.Router
                && peer.Role == ZLinkLocationRole.Router
                && LocalIsInitiator(local, peer),
            // A router never dials out to dealers; dealers dial routers.
            ZLinkLocationAutoConnectType.ClientServer =>
                local.Role == ZLinkLocationRole.Dealer && peer.Role == ZLinkLocationRole.Router,
            ZLinkLocationAutoConnectType.DealerMesh =>
                local.Role == ZLinkLocationRole.Dealer
                && peer.Role == ZLinkLocationRole.Dealer
                && LocalIsInitiator(local, peer),
            // A publisher never dials out to subscribers.
            ZLinkLocationAutoConnectType.Fanout =>
                local.Role == ZLinkLocationRole.Sub && peer.Role == ZLinkLocationRole.Pub,
            ZLinkLocationAutoConnectType.SpotMesh =>
                local.Role == ZLinkLocationRole.Spot
                && peer.Role == ZLinkLocationRole.Spot
                && LocalIsInitiator(local, peer),
            _ => false
        };

    /// <summary>
    /// Pairwise initiator for symmetric meshes: compare routing ids by byte
    /// order when both exist, otherwise compare endpoints ordinally; only
    /// the smaller side dials so the pair never double-connects. A member
    /// with no endpoint cannot be dialed, so it always initiates toward
    /// dialable peers regardless of the order.
    /// </summary>
    private static bool LocalIsInitiator(ZLinkAutoConnectLocal local, ZLinkPeerLocation peer)
    {
        if (string.IsNullOrEmpty(local.Endpoint))
        {
            return true;
        }

        if (local.NodeRid is { Size: > 0 } localRid && peer.NodeRid is { Size: > 0 } peerRid)
        {
            var byRid = string.CompareOrdinal(localRid.ToHex(), peerRid.ToHex());
            if (byRid != 0)
            {
                return byRid < 0;
            }
        }

        return string.CompareOrdinal(local.Endpoint, peer.Endpoint) < 0;
    }
}
