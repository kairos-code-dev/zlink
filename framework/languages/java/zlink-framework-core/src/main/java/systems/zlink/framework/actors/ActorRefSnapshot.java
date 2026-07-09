package systems.zlink.framework.actors;

import systems.zlink.contracts.core.RoutingId;

public record ActorRefSnapshot(RoutingId nodeRid, String actorId, long generation) {
    public static ActorRefSnapshot from(ActorRef actorRef) {
        return new ActorRefSnapshot(
            actorRef.nodeRid(),
            actorRef.actorId(),
            actorRef.generation());
    }

    public ActorRef toActorRef() {
        return new ActorRef(nodeRid, actorId, generation);
    }
}
