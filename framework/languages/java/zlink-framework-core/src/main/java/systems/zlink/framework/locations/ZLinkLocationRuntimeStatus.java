package systems.zlink.framework.locations;

import java.time.Duration;
import java.time.Instant;

public record ZLinkLocationRuntimeStatus(
    boolean storeHealthy,
    boolean watchEnabled,
    Duration pollingInterval,
    Instant lastRefreshAt,
    String lastError,
    boolean ownerLeaseHealthy,
    Instant ownerLeaseRenewedAt) {
}
