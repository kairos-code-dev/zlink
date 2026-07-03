package systems.zlink.framework.locations;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotKind;

public record ZLinkSpotLocationFilter(
    String meshName,
    String spotType,
    RoutingId nodeRid,
    ZLinkSpotKind spotKind) {

    public static ZLinkSpotLocationFilter all() {
        return new ZLinkSpotLocationFilter(null, null, null, null);
    }
}
