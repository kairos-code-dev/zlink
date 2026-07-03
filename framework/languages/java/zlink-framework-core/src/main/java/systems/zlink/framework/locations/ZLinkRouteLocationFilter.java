package systems.zlink.framework.locations;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkRouteLocationFilter(
    ZLinkRouteKind routeKind,
    RoutingId ownerNodeRid,
    String ownerId) {

    public static ZLinkRouteLocationFilter all() {
        return new ZLinkRouteLocationFilter(null, null, null);
    }
}
