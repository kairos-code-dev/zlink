package systems.zlink.framework.locations;

import java.time.Instant;
import java.util.List;

public record ZLinkOwnerLeaseSnapshot(
    List<ZLinkOwnerLease> leases,
    Instant storeNow) {
}
