package systems.zlink.framework.spots;

import systems.zlink.contracts.core.RoutingId;

public record SpotRemoteRef(
    String routerChannelId,
    RoutingId targetNodeRid,
    RoutingId spotRid,
    ZLinkSpotKind spotKind) {
}
