package systems.zlink.framework.locations;

import java.time.Instant;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkLocationTopologyEntry(
    ZLinkLocationKind kind,
    String meshName,
    ZLinkLocationRole role,
    RoutingId nodeRid,
    String spotId,
    String actorId,
    String endpoint,
    ZLinkLocationTopologyState state,
    long desiredCount,
    long readyCount,
    int errorCode,
    Instant updatedAt) {
}
