package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.Optional;

public record ZLinkMeshDrainSnapshot(
    ZLinkMeshNodeState state,
    Optional<Instant> deadline,
    boolean workSealed,
    long pendingRequestCount,
    long pendingTransferCount,
    long pendingStreamBarrierCount) {
}
