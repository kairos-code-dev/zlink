package systems.zlink.framework.locations;

public record ZLinkLocationWatchFilter(
    ZLinkLocationKind kind,
    String meshName,
    ZLinkRouteKind routeKind) {
}
