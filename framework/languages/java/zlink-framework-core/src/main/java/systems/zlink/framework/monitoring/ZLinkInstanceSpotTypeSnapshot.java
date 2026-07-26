package systems.zlink.framework.monitoring;

import java.util.Optional;

public record ZLinkInstanceSpotTypeSnapshot(
    String instanceSpotType,
    long activeCount,
    long activatingCount,
    long closingCount,
    long pendingMessageCount,
    long pendingByteCount,
    Optional<String> lastActivationOutcome) {
}
