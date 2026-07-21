package systems.zlink.framework.locations;

import java.util.Map;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkAllocatedRoutingId(
    String groupName,
    int slot,
    Map<String, RoutingId> meshNodeRoutingIds) {
    public ZLinkAllocatedRoutingId {
        meshNodeRoutingIds = Map.copyOf(meshNodeRoutingIds);
    }
}
