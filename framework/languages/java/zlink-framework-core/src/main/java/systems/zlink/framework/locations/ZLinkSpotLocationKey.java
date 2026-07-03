package systems.zlink.framework.locations;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkSpotLocationKey(
    String meshName,
    RoutingId spotRid) {
}
