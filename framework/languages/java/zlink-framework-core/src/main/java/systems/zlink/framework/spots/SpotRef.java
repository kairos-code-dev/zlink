package systems.zlink.framework.spots;

import java.util.Objects;
import systems.zlink.contracts.core.RoutingId;

public record SpotRef(
    RoutingId spotRid,
    long objectGeneration,
    String meshName,
    RoutingId nodeRid) {
    public SpotRef {
        Objects.requireNonNull(spotRid, "spotRid");
        Objects.requireNonNull(nodeRid, "nodeRid");
        if (objectGeneration <= 0) {
            throw new IllegalArgumentException(
                "objectGeneration must be positive");
        }
        if (meshName == null || meshName.isBlank()) {
            throw new IllegalArgumentException("meshName must not be blank");
        }
    }
}
