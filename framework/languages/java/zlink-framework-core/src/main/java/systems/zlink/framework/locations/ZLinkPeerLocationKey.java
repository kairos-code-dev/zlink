package systems.zlink.framework.locations;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkPeerLocationKey(
    ZLinkLocationAutoConnectType autoConnectType,
    String meshName,
    ZLinkLocationRole role,
    RoutingId nodeRid,
    String endpoint) {
}
