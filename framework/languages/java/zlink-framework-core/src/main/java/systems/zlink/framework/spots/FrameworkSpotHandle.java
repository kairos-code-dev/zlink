package systems.zlink.framework.spots;

import java.util.Objects;
import systems.zlink.contracts.core.RoutingId;

record FrameworkSpotHandle(RoutingId spotRid) implements SpotHandle {
    FrameworkSpotHandle {
        Objects.requireNonNull(spotRid, "spotRid");
    }
}
