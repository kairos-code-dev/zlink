package systems.zlink.framework.locations;

import java.time.Instant;

public record ZLinkLocationChanged(
    ZLinkLocationKind kind,
    String locationKey,
    ZLinkLocationChangeType changeType,
    long generation,
    Instant updatedAt) {
}
