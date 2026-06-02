package systems.zlink.framework.runtime.backend;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkBackendSpotNodePeerEntry(RoutingId peerRid, String endpoint, String state) {
}
