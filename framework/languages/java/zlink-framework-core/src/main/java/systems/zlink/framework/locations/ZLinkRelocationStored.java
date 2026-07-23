package systems.zlink.framework.locations;

import java.time.Instant;

public record ZLinkRelocationStored(
    String reference,
    long checksumCrc32c,
    Instant expiresAt,
    Instant storeNow) {
}
