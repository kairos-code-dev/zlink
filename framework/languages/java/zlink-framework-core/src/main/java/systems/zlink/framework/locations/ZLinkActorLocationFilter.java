package systems.zlink.framework.locations;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotKind;

public record ZLinkActorLocationFilter(
    String actorType,
    RoutingId nodeRid,
    RoutingId spotRid,
    ZLinkSpotKind locationKind) {

    public static ZLinkActorLocationFilter all() {
        return new ZLinkActorLocationFilter(null, null, null, null);
    }
}
