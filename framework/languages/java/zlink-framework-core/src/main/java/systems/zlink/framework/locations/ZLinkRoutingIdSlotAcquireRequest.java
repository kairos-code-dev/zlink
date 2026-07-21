package systems.zlink.framework.locations;

import java.time.Duration;
import java.util.List;

public record ZLinkRoutingIdSlotAcquireRequest(
    String groupName,
    List<ZLinkRoutingIdSlotAllocationMember> members,
    int slotCount,
    String ownerId,
    Duration leaseTtl) {
    public ZLinkRoutingIdSlotAcquireRequest {
        members = List.copyOf(members);
    }
}
