package systems.zlink.framework.monitoring;

import java.util.List;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkMeshPeerSnapshot(
    RoutingId rid,
    long lifecycleGeneration,
    long descriptorRevision,
    String endpoint,
    String admissionState,
    boolean ready,
    String drainState,
    List<String> channelNames,
    Optional<String> lastFailure) {
    public ZLinkMeshPeerSnapshot {
        channelNames = List.copyOf(channelNames);
    }
}
