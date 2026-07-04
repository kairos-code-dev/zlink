package systems.zlink.framework.actors;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkActorRef(RoutingId nodeRid, String actorId, long generation) {
}
