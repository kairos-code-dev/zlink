package systems.zlink.framework.locations;

import java.time.Instant;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.spots.ZLinkSpotKind;

public record ZLinkActorLocation(
    String actorId,
    String actorType,
    ZLinkActorRef actorRef,
    RoutingId nodeRid,
    ZLinkSpotKind locationKind,
    String spotMeshName,
    RoutingId spotRid,
    String ownerId,
    long generation,
    Instant updatedAt) {
}
