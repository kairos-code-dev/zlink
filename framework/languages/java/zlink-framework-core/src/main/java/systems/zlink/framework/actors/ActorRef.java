package systems.zlink.framework.actors;

import systems.zlink.contracts.core.RoutingId;

public record ActorRef(RoutingId nodeRid, String actorId, long generation) {
}
