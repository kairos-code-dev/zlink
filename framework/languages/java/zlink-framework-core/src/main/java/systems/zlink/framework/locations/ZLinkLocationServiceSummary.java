package systems.zlink.framework.locations;

import java.time.Instant;

public record ZLinkLocationServiceSummary(
    String meshName,
    ZLinkLocationAutoConnectType autoConnectType,
    ZLinkLocationRole role,
    long totalCount,
    long readyCount,
    long errorCount,
    long stoppedCount,
    Instant lastUpdatedAt) {
}
