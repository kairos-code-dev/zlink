package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.List;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkMeshNodeObjectRole;
import systems.zlink.framework.locations.ZLinkObjectCapability;
import systems.zlink.framework.locations.ZLinkPlacementCapacity;

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
    Optional<String> lastPlacementReservationFailure,
    ZLinkMeshDrainSnapshot drain) {
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

    public ZLinkMeshNodeSnapshot(
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
        this(
            meshName,
            rid,
            lifecycleGeneration,
            descriptorRevision,
            endpoint,
            state,
            sequence,
            observedAt,
            descriptorSources,
            peers,
            channels,
            instanceSpots,
            claims,
            location,
            objectRole,
            placementWeight,
            objectCapacity,
            activationConcurrency,
            objectCapabilities,
            placementReservationFailureCount,
            lastPlacementReservationFailure,
            new ZLinkMeshDrainSnapshot(
                state,
                Optional.empty(),
                state == ZLinkMeshNodeState.DRAINED || state == ZLinkMeshNodeState.STOPPED,
                claims.pendingApplicationWork(),
                0,
                0));
    }
}
