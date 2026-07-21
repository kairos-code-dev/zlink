package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkMeshRuntimeEvent(
    String identifier,
    long sequence,
    Instant timestamp,
    String meshName,
    RoutingId sourceRid,
    Optional<RoutingId> peerRid,
    Optional<Long> lifecycleGeneration,
    Optional<Long> descriptorRevision,
    Optional<String> channelName,
    Optional<String> claimDomain,
    Optional<String> messageKind,
    Optional<Long> remoteSnapshotCount,
    Optional<Long> remoteAdmittedCount,
    Optional<Long> remoteDroppedCount,
    Optional<Long> localSnapshotCount,
    Optional<Long> localAdmittedCount,
    Optional<Long> localDroppedCount,
    Optional<String> reason,
    Optional<ZLinkMeshNodeState> state) {
}
