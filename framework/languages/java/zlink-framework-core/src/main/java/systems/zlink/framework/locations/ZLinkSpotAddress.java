package systems.zlink.framework.locations;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkSpotAddress(
    String meshName,
    RoutingId nodeRid,
    RoutingId spotRid) {
}
