package systems.zlink.framework.runtime.internal.spots;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotKind;

public record SpotTransportAddress(
    String routerChannelId,
    RoutingId targetNodeRid,
    RoutingId spotRid,
    long spotGeneration,
    long authorityOwnerGeneration,
    ZLinkSpotKind spotKind) {
    public SpotTransportAddress(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        ZLinkSpotKind spotKind) {
        this(
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            0L,
            spotKind);
    }
}
