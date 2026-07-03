package systems.zlink.framework.locations;

import java.time.Instant;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotKind;

public record ZLinkActorLocation(
    String actorType,
    String actorId,
    String actorRef,
    RoutingId nodeRid,
    long generation,
    ZLinkSpotKind locationKind,
    String spotMeshName,
    RoutingId spotRid,
    ZLinkSpotKind spotKind,
    String ownerId,
    Instant updatedAt) {
}
