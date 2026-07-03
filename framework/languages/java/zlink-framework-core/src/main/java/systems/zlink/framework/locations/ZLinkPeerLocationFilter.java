package systems.zlink.framework.locations;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkPeerLocationFilter(
    ZLinkLocationAutoConnectType autoConnectType,
    String meshName,
    ZLinkLocationRole role,
    RoutingId nodeRid,
    String endpoint) {

    public static ZLinkPeerLocationFilter all() {
        return new ZLinkPeerLocationFilter(null, null, null, null, null);
    }
}
