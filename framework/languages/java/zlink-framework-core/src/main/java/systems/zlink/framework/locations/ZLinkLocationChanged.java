package systems.zlink.framework.locations;

import java.time.Instant;

public record ZLinkLocationChanged(
    ZLinkLocationKind kind,
    ZLinkLocationKey key,
    ZLinkLocationChangeType changeType,
    long generation,
    Instant updatedAt) {
}
