package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.List;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkMeshNodeSnapshot(
    String meshName,
    RoutingId rid,
    long lifecycleGeneration,
    long descriptorRevision,
    String endpoint,
    ZLinkMeshNodeState state,
    long sequence,
    Instant observedAt,
    List<String> descriptorSources,
    List<ZLinkMeshPeerSnapshot> peers,
    List<ZLinkMeshChannelSnapshot> channels,
    ZLinkMeshClaimSnapshot claims,
    ZLinkLocationRuntimeSnapshot location,
    ZLinkMeshDrainSnapshot drain) {
    public ZLinkMeshNodeSnapshot {
        descriptorSources = List.copyOf(descriptorSources);
        peers = List.copyOf(peers);
        channels = List.copyOf(channels);
    }
}
