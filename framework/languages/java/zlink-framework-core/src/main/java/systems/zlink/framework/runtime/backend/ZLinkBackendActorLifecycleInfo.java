package systems.zlink.framework.runtime.backend;

import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkBackendActorLifecycleInfo(
    ZLinkBackendActorRef previousActor,
    ZLinkBackendActorRef currentActor,
    Optional<RoutingId> previousSpotRid,
    Optional<RoutingId> currentSpotRid,
    long joinEpoch,
    int flags) {
    public ZLinkBackendActorLifecycleInfo {
        previousSpotRid = previousSpotRid == null ? Optional.empty() : previousSpotRid;
        currentSpotRid = currentSpotRid == null ? Optional.empty() : currentSpotRid;
    }
}
