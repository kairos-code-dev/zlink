package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.Optional;

public record ZLinkLocationRuntimeSnapshot(
    String state,
    Optional<Instant> lastSuccessAt,
    Optional<Instant> lastFailureAt) {
}
