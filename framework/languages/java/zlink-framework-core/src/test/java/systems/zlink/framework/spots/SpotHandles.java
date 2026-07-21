package systems.zlink.framework.spots;

import systems.zlink.contracts.core.RoutingId;

public final class SpotHandles {
    private SpotHandles() {
    }

    public static SpotHandle create(RoutingId spotRid) {
        return new FrameworkSpotHandle("test-mesh", spotRid, spotRid, 1L);
    }
}
