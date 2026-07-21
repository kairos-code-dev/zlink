package systems.zlink.framework.locations;

import java.util.List;

public record ZLinkRoutingIdSlotGroupConfigurationMismatch(
    List<ZLinkRoutingIdSlotAllocationMember> expectedMembers,
    int expectedSlotCount,
    List<ZLinkRoutingIdSlotAllocationMember> actualMembers,
    int actualSlotCount)
    implements ZLinkRoutingIdSlotAcquireResult {
    public ZLinkRoutingIdSlotGroupConfigurationMismatch {
        expectedMembers = List.copyOf(expectedMembers);
        actualMembers = List.copyOf(actualMembers);
    }
}
