package systems.zlink.framework.runtime.backend;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkBackendActorJoinEntrySpotResult(
    ZLinkBackendRequestResult result,
    ZLinkBackendActorRef actor,
    RoutingId targetNodeRid,
    long joinEpoch,
    int flags) {
}
