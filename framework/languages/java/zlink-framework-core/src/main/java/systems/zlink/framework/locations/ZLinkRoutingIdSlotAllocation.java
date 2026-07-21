package systems.zlink.framework.locations;

import java.time.Instant;

public record ZLinkRoutingIdSlotAllocation(
    int slot,
    ZLinkLocationOwnerToken owner,
    Instant leaseExpiresAt,
    Instant storeNow) {
}
