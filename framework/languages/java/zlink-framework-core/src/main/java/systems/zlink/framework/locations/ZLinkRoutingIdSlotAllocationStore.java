package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkRoutingIdSlotAllocationStore {
    CompletionStage<ZLinkRoutingIdSlotAcquireResult> acquireRoutingIdSlot(
        ZLinkRoutingIdSlotAcquireRequest request);

    CompletionStage<ZLinkRoutingIdSlotReleaseResult> releaseRoutingIdSlot(
        String groupName,
        int slot,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkRoutingIdSlotAllocationSnapshot> listRoutingIdSlots(
        String groupName);
}
