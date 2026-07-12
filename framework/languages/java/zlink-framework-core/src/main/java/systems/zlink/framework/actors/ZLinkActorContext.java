package systems.zlink.framework.actors;

import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;

public interface ZLinkActorContext {
    Optional<RoutingId> spotRid();

    ZLinkBoundSession boundSession();

    ZLinkActorJoinCall joinSpot(RoutingId spotRid, Object request);

    ZLinkActorJoinCall joinEntrySpot(RoutingId spotNodeRid, Object request);
}
