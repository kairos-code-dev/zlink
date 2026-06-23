package systems.zlink.framework.spots;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.messaging.ZLinkMessage;

public record ZLinkSpotCreateResult(
    RoutingId spotRid,
    ZLinkSpotCreateState state,
    ZLinkMessage reply) {
}
