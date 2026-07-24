package systems.zlink.framework.locations;

import java.time.Instant;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotKind;

public record ZLinkSpotLocation(
    String meshName,
    String spotId,
    long spotGeneration,
    String spotType,
    RoutingId nodeRid,
    ZLinkSpotKind spotKind,
    String routeEndpoint,
    String ownerId,
    long generation,
    Instant updatedAt) {
    public ZLinkSpotLocation {
        systems.zlink.framework.runtime.internal.spots.ZLinkSpotIdValidator
            .requireValid(spotId);
    }

    public ZLinkSpotLocation(
        String meshName,
        String spotId,
        String spotType,
        RoutingId nodeRid,
        ZLinkSpotKind spotKind,
        String routeEndpoint,
        String ownerId,
        long generation,
        Instant updatedAt) {
        this(
            meshName,
            spotId,
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
