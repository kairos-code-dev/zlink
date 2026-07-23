package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.Optional;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState;
import systems.zlink.framework.runtime.host.ZLinkTerminationIntent;
import systems.zlink.framework.runtime.host.ZLinkTerminationReason;
import systems.zlink.framework.runtime.host.ZLinkTerminationResult;

public record ZLinkFrameworkRuntimeSnapshot(
    ZLinkFrameworkRuntimeState state,
    Optional<ZLinkTerminationIntent> effectiveIntent,
    Optional<Instant> deadline,
    boolean workSealed,
    Optional<ZLinkTerminationReason> blockerReason,
    long pendingRequestCount,
    long pendingRelocationCount,
    long pendingStreamBarrierCount,
    Optional<ZLinkTerminationResult> terminalResult,
    long sequence,
    Instant observedAt) {
}
