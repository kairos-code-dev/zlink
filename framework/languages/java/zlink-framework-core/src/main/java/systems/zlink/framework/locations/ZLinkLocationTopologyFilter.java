package systems.zlink.framework.locations;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkLocationTopologyFilter(
    ZLinkLocationKind kind,
    String meshName,
    ZLinkLocationRole role,
    RoutingId nodeRid,
    ZLinkLocationTopologyState state) {

    public static ZLinkLocationTopologyFilter all() {
        return new ZLinkLocationTopologyFilter(null, null, null, null, null);
    }
}
