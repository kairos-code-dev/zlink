package systems.zlink.framework.locations;

import java.time.Instant;

public record ZLinkRelocationRenewed(
    Instant expiresAt,
    Instant storeNow)
    implements ZLinkRelocationRenewResult {
}
