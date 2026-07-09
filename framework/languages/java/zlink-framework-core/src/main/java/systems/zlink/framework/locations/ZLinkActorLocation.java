package systems.zlink.framework.locations;

import java.time.Instant;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.spots.ZLinkSpotKind;

public record ZLinkActorLocation(
    String actorId,
    String actorType,
    ActorRef actorRef,
    RoutingId nodeRid,
    ZLinkSpotKind locationKind,
    String spotMeshName,
    RoutingId spotRid,
    String ownerId,
    long generation,
    Instant updatedAt) {
    public static ZLinkActorLocation fromActorRef(
        String actorId,
        String actorType,
        ActorRef actorRef,
        RoutingId nodeRid,
        ZLinkSpotKind locationKind,
        String spotMeshName,
        RoutingId spotRid,
        String ownerId,
        long generation,
        Instant updatedAt) {
        return new ZLinkActorLocation(
            actorId,
            actorType,
            actorRef,
            nodeRid,
            locationKind,
            spotMeshName,
            spotRid,
            ownerId,
            generation,
            updatedAt);
    }
}
