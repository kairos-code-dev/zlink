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
    String spotId,
    String ownerId,
    long generation,
    Instant updatedAt) {
    public ZLinkActorLocation {
        if (spotId != null) {
            systems.zlink.framework.runtime.internal.spots.ZLinkSpotIdValidator
                .requireValid(spotId);
        }
    }

    public static ZLinkActorLocation fromActorRef(
        String actorId,
        String actorType,
        ActorRef actorRef,
        RoutingId nodeRid,
        ZLinkSpotKind locationKind,
        String spotMeshName,
        String spotId,
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
            spotId,
            ownerId,
            generation,
            updatedAt);
    }
}
