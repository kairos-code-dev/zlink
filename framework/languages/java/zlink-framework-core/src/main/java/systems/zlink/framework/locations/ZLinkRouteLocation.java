package systems.zlink.framework.locations;

import java.time.Instant;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkRouteLocation(
    ZLinkRouteKind routeKind,
    String routeKey,
    RoutingId ownerNodeRid,
    String ownerId,
    long generation,
    byte[] value,
    Instant updatedAt) {
}
