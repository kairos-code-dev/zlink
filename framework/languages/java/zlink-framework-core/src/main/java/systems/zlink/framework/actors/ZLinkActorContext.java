package systems.zlink.framework.actors;

import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;

public interface ZLinkActorContext {
    Optional<String> spotId();

    ZLinkBoundSession boundSession();

    ZLinkActorJoinCall joinSpot(String spotId, Object request);

    ZLinkActorJoinCall joinEntrySpot(RoutingId spotNodeRid, Object request);
}
