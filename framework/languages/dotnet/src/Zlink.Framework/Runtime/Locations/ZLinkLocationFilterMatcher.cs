namespace Zlink.Framework.Runtime.Locations;

internal static class ZLinkLocationFilterMatcher
{
    public static bool Matches(ZLinkPeerLocation row, ZLinkPeerLocationFilter filter) =>
        (filter.AutoConnectType is null || row.AutoConnectType == filter.AutoConnectType)
        && (filter.MeshName is null || row.MeshName == filter.MeshName)
        && (filter.Role is null || row.Role == filter.Role)
        && (filter.NodeRid is null || Equals(row.NodeRid, filter.NodeRid))
        && (filter.Endpoint is null || row.Endpoint == filter.Endpoint);

    public static bool Matches(ZLinkSpotLocation row, ZLinkSpotLocationFilter filter) =>
        (filter.MeshName is null || row.MeshName == filter.MeshName)
        && (filter.SpotType is null || row.SpotType == filter.SpotType)
        && (filter.NodeRid is null || row.NodeRid.Equals(filter.NodeRid.Value))
        && (filter.SpotKind is null || row.SpotKind == filter.SpotKind);

    public static bool Matches(ZLinkActorLocation row, ZLinkActorLocationFilter filter) =>
        (filter.ActorType is null || row.ActorType == filter.ActorType)
        && (filter.NodeRid is null || row.NodeRid.Equals(filter.NodeRid.Value))
        && (filter.SpotRid is null || Equals(row.SpotRid, filter.SpotRid))
        && (filter.LocationKind is null || row.LocationKind == filter.LocationKind);

    public static bool Matches(ZLinkRouteLocation row, ZLinkRouteLocationFilter filter) =>
        (filter.RouteKind is null || row.RouteKind == filter.RouteKind)
        && (filter.OwnerNodeRid is null || row.OwnerNodeRid.Equals(filter.OwnerNodeRid.Value))
        && (filter.OwnerId is null || row.OwnerId == filter.OwnerId);
}
