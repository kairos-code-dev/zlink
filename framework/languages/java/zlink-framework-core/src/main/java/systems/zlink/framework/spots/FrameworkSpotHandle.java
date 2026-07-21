package systems.zlink.framework.spots;

import java.util.Objects;
import systems.zlink.contracts.core.RoutingId;

record FrameworkSpotHandle(
    String meshName,
    RoutingId spotRid,
    RoutingId ownerNodeRid,
    long spotGeneration) implements SpotHandle {
    FrameworkSpotHandle {
        Objects.requireNonNull(meshName, "meshName");
        Objects.requireNonNull(spotRid, "spotRid");
        Objects.requireNonNull(ownerNodeRid, "ownerNodeRid");
        if (spotGeneration <= 0) {
            throw new IllegalArgumentException("spotGeneration must be positive");
        }
    }
}
