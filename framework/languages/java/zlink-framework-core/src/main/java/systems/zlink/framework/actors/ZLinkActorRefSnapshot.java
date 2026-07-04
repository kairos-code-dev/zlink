package systems.zlink.framework.actors;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkActorRefSnapshot(
    RoutingId nodeRid,
    String actorId,
    long generation) {
    public static ZLinkActorRefSnapshot from(ZLinkActorRef actorRef) {
        return new ZLinkActorRefSnapshot(
            actorRef.nodeRid(),
            actorRef.actorId(),
            actorRef.generation());
    }

    public ZLinkActorRef toActorRef() {
        return new ZLinkActorRef(nodeRid, actorId, generation);
    }
}
