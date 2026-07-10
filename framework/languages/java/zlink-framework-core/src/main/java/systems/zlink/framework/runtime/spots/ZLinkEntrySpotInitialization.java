package systems.zlink.framework.runtime.spots;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
record EntrySpotInitialization(
    RoutingId nodeRid,
    ZLinkBackendSpot backendSpot,
    SpotNodeRegistration registration) {
}
