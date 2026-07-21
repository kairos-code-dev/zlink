package systems.zlink.framework.locations;

public record ZLinkRoutingIdSlotAcquired(
    ZLinkRoutingIdSlotAllocation allocation)
    implements ZLinkRoutingIdSlotAcquireResult {
}
