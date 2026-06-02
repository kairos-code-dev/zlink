package systems.zlink.framework.runtime;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkBackendActorRef(RoutingId nodeRid, String actorId, long epoch) {
}
