package systems.zlink.framework.locations;

import java.time.Instant;
import java.util.List;

public record ZLinkRoutingIdSlotAllocationSnapshot(
    String groupName,
    List<ZLinkRoutingIdSlotAllocationMember> members,
    int slotCount,
    List<ZLinkRoutingIdSlotAllocation> allocations,
    Instant storeNow) {
    public ZLinkRoutingIdSlotAllocationSnapshot {
        members = List.copyOf(members);
        allocations = List.copyOf(allocations);
    }
}
