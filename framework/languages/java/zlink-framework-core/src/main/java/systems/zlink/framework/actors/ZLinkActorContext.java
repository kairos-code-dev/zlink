package systems.zlink.framework.actors;

import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.spots.ZLinkSpot;

public interface ZLinkActorContext {
    Optional<RoutingId> spotRid();

    boolean isJoined();

    ZLinkBoundSession boundSession();

    ZLinkSpot<?> getSpot();

    <TSpot extends ZLinkSpot<?>> TSpot getSpot(Class<TSpot> spotType);

    default ZLinkActorJoinSpotCall joinSpot(RoutingId spotRid, Object request) {
        throw new ZLinkConfigurationException(
            "joinSpot is only available on framework-managed actor contexts");
    }

    default ZLinkActorJoinEntrySpotCall joinEntrySpot(RoutingId spotNodeRid, Object request) {
        throw new ZLinkConfigurationException(
            "joinEntrySpot is only available on framework-managed actor contexts");
    }
}
