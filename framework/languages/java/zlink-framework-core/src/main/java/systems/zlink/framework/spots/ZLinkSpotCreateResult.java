package systems.zlink.framework.spots;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkSpotCreateResult(
    RoutingId spotRid,
    boolean created) {
}
