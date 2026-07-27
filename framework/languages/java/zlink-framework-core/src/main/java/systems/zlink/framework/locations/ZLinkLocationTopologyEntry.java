package systems.zlink.framework.locations;

import java.time.Instant;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkLocationTopologyEntry(
    String meshName,
    RoutingId nodeRid,
    String endpoint,
    boolean draining,
    ZLinkLocationTopologyState state,
    Instant updatedAt) {
}
