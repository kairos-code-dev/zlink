package systems.zlink.framework.runtime.channels;

import java.util.Objects;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;

interface ZLinkSpotRouteTarget {
}

record ZLinkRouteBridgeTarget() implements ZLinkSpotRouteTarget {
}

record ZLinkSpotRouterNodeTarget(ZLinkBackendSpotNode node) implements ZLinkSpotRouteTarget {
    ZLinkSpotRouterNodeTarget {
        Objects.requireNonNull(node, "node");
    }
}
