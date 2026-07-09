package systems.zlink.framework.locations;

import systems.zlink.contracts.core.RoutingId;

public record SpotRef(String meshName, RoutingId nodeRid, RoutingId spotRid) {
}
