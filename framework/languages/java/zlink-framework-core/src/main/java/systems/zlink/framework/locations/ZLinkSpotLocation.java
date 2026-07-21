package systems.zlink.framework.locations;

import java.time.Instant;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotKind;

public record ZLinkSpotLocation(
    String meshName,
    RoutingId spotRid,
    long spotGeneration,
    String spotType,
    RoutingId nodeRid,
    ZLinkSpotKind spotKind,
    String routeEndpoint,
    String ownerId,
    long generation,
    Instant updatedAt) {

    public ZLinkSpotLocation(
        String meshName,
        RoutingId spotRid,
        String spotType,
        RoutingId nodeRid,
        ZLinkSpotKind spotKind,
        String routeEndpoint,
        String ownerId,
        long generation,
        Instant updatedAt) {
        this(
            meshName,
            spotRid,
            generation,
            spotType,
            nodeRid,
            spotKind,
            routeEndpoint,
            ownerId,
            generation,
            updatedAt);
    }
}
