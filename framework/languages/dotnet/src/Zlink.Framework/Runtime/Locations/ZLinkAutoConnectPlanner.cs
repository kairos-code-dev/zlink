namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// A local capability that participates in auto connect: what this node
/// advertises for one mesh and what it dials from. The auto-connect type
/// and role classify the local side only; remote MeshNode descriptors carry
/// no role — publishing a descriptor in a mesh namespace is what makes a
/// node a dial target of that mesh (40-location-runtime §5).
/// </summary>
internal sealed record ZLinkAutoConnectLocal(
    ZLinkLocationAutoConnectType AutoConnectType,
    string MeshName,
    ZLinkLocationRole Role,
    RoutingId? NodeRid,
    string Endpoint);

/// <summary>
/// One connect target chosen by the planner, keyed by the descriptor RID so
/// an endpoint change under the same key is treated as a handover.
/// </summary>
internal sealed record ZLinkAutoConnectTarget(
    string TargetKey,
    RoutingId NodeRid,
    string Endpoint,
    bool Draining = false,
    string? OwnerId = null,
    bool InitiatesSpotRouterLink = true);

/// <summary>
/// Pure desired-target-set computation over a mesh descriptor snapshot,
/// with the pairwise initiator order for symmetric meshes. No I/O, so every
/// rule is unit-testable in isolation.
/// </summary>
internal static class ZLinkAutoConnectPlanner
{
    internal static IReadOnlyDictionary<string, ZLinkAutoConnectTarget> ComputeDesired(
        ZLinkAutoConnectLocal local,
        IReadOnlyList<ZLinkMeshNodeDescriptor> descriptors)
    {
        var desired = new Dictionary<string, ZLinkAutoConnectTarget>(StringComparer.Ordinal);
        foreach (var descriptor in descriptors)
        {
            if (!string.Equals(descriptor.MeshName, local.MeshName, StringComparison.Ordinal)
                || string.IsNullOrEmpty(descriptor.Endpoint)
                || IsSelf(local, descriptor)
                || !ShouldDial(local, descriptor))
            {
                continue;
            }

            var target = new ZLinkAutoConnectTarget(
                descriptor.Rid.ToHex(),
                descriptor.Rid,
                descriptor.Endpoint,
                descriptor.Draining,
                descriptor.OwnerId,
                local.AutoConnectType != ZLinkLocationAutoConnectType.SpotMesh
                || LocalIsInitiator(local, descriptor));
            desired[target.TargetKey] = target;
        }

        return desired;
    }

    internal static int CountDiscoveredPeers(
        ZLinkAutoConnectLocal local,
        IReadOnlyList<ZLinkMeshNodeDescriptor> descriptors)
    {
        return descriptors.Count(descriptor =>
            string.Equals(descriptor.MeshName, local.MeshName, StringComparison.Ordinal)
            && !string.IsNullOrEmpty(descriptor.Endpoint)
            && !IsSelf(local, descriptor));
    }

    private static bool IsSelf(ZLinkAutoConnectLocal local, ZLinkMeshNodeDescriptor descriptor)
    {
        if (local.NodeRid is { Size: > 0 } localRid && localRid.Equals(descriptor.Rid))
        {
            return true;
        }

        return string.Equals(descriptor.Endpoint, local.Endpoint, StringComparison.Ordinal);
    }

    /// <summary>
    /// Dial decisions come from the local role alone. Advertise-only roles
    /// (server routers, publishers) never dial; dial-only roles (dealers,
    /// subscribers) dial every descriptor of the mesh; symmetric meshes use
    /// the pairwise initiator so a pair never double-connects — both sides
    /// dialing creates two links for one routing id and breaks
    /// rid-addressed requests.
    /// </summary>
    private static bool ShouldDial(ZLinkAutoConnectLocal local, ZLinkMeshNodeDescriptor descriptor) =>
        local.AutoConnectType switch
        {
            ZLinkLocationAutoConnectType.RouteMesh =>
                local.Role == ZLinkLocationRole.Router && LocalIsInitiator(local, descriptor),
            ZLinkLocationAutoConnectType.ClientServer => local.Role == ZLinkLocationRole.Dealer,
            ZLinkLocationAutoConnectType.DealerMesh =>
                local.Role == ZLinkLocationRole.Dealer && LocalIsInitiator(local, descriptor),
            ZLinkLocationAutoConnectType.Fanout => local.Role == ZLinkLocationRole.Sub,
            ZLinkLocationAutoConnectType.SpotMesh => local.Role == ZLinkLocationRole.Spot,
            _ => false
        };

    /// <summary>
    /// Pairwise initiator for symmetric meshes: compare routing ids by byte
    /// order when both exist, otherwise compare endpoints ordinally; only
    /// the smaller side dials so the pair never double-connects. A member
    /// with no endpoint cannot be dialed, so it always initiates toward
    /// dialable peers regardless of the order.
    /// </summary>
    private static bool LocalIsInitiator(ZLinkAutoConnectLocal local, ZLinkMeshNodeDescriptor descriptor)
    {
        if (string.IsNullOrEmpty(local.Endpoint))
        {
            return true;
        }

        if (local.NodeRid is { Size: > 0 } localRid)
        {
            var byRid = string.CompareOrdinal(localRid.ToHex(), descriptor.Rid.ToHex());
            if (byRid != 0)
            {
                return byRid < 0;
            }
        }

        return string.CompareOrdinal(local.Endpoint, descriptor.Endpoint) < 0;
    }
}
