package systems.zlink.framework.runtime.backend;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkBackendSpotRoute(RoutingId nodeRid, RoutingId spotRid) {
}
