package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.List;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkMeshNodeObjectRole;
import systems.zlink.framework.locations.ZLinkObjectCapability;
import systems.zlink.framework.locations.ZLinkPlacementCapacity;
import systems.zlink.framework.locations.ZLinkActivationConcurrency;

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
    List<ZLinkInstanceSpotTypeSnapshot> instanceSpots,
    ZLinkMeshClaimSnapshot claims,
    ZLinkLocationRuntimeSnapshot location,
    ZLinkMeshNodeObjectRole objectRole,
    int placementWeight,
    ZLinkPlacementCapacity objectCapacity,
    ZLinkActivationConcurrency activationConcurrency,
    List<ZLinkObjectCapability> objectCapabilities,
    long placementReservationFailureCount,
    Optional<String> lastPlacementReservationFailure) {
    public ZLinkMeshNodeSnapshot {
        descriptorSources = List.copyOf(descriptorSources);
        peers = List.copyOf(peers);
        channels = List.copyOf(channels);
        instanceSpots = List.copyOf(instanceSpots);
        objectCapabilities = List.copyOf(objectCapabilities);
        lastPlacementReservationFailure = java.util.Objects.requireNonNull(
            lastPlacementReservationFailure,
            "lastPlacementReservationFailure");
    }

}
