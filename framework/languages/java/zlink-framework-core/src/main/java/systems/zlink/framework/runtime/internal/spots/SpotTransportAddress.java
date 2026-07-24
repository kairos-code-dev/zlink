package systems.zlink.framework.runtime.internal.spots;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotKind;

public record SpotTransportAddress(
    String routerChannelId,
    RoutingId targetNodeRid,
    String spotId,
    long spotGeneration,
    long authorityOwnerGeneration,
    ZLinkSpotKind spotKind) {
    public SpotTransportAddress(
        String routerChannelId,
        RoutingId targetNodeRid,
        String spotId,
        long spotGeneration,
        ZLinkSpotKind spotKind) {
        this(
            routerChannelId,
            targetNodeRid,
            spotId,
            spotGeneration,
            0L,
            spotKind);
    }
}
