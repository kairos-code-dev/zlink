package systems.zlink.framework.actors;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkActorPlacement(
    RoutingId preferredNodeRid,
    String routeMesh) {
    public static ZLinkActorPlacement any() {
        return new ZLinkActorPlacement(null, null);
    }
}
