package systems.zlink.framework.locations;

import java.time.Instant;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.runtime.host.ZLinkFrameworkRuntimeState;

/**
 * Store-backed identity and placement capability of one physical MeshNode
 * lifecycle.
 */
public record ZLinkMeshNodeDescriptor(
    String meshName,
    RoutingId rid,
    long lifecycleGeneration,
    long descriptorRevision,
    String endpoint,
    Map<String, Integer> channelWeights,
    long applicationVersion,
    List<ZLinkObjectCapability> objectCapabilities,
    ZLinkMeshNodeObjectRole objectRole,
    int placementWeight,
    ZLinkPlacementCapacity capacity,
    Optional<String> maintenanceWave,
    ZLinkFrameworkRuntimeState state,
    String securityIdentity,
    String ownerId,
    long leaseGeneration,
    Instant updatedAt) {
    public ZLinkMeshNodeDescriptor {
        meshName = requireText(meshName, "meshName");
        Objects.requireNonNull(rid, "rid");
        endpoint = requireText(endpoint, "endpoint");
        securityIdentity = requireText(
            securityIdentity,
            "securityIdentity");
        ownerId = requireText(ownerId, "ownerId");
        if (lifecycleGeneration <= 0
            || descriptorRevision <= 0
            || leaseGeneration <= 0) {
            throw new IllegalArgumentException(
                "lifecycleGeneration, descriptorRevision and "
                    + "leaseGeneration must be positive");
        }
        if (applicationVersion < 0) {
            throw new IllegalArgumentException(
                "applicationVersion must not be negative");
        }
        if (placementWeight < 0 || placementWeight > 100) {
            throw new IllegalArgumentException(
                "placementWeight must be in 0..100");
        }
        channelWeights = Map.copyOf(
            Objects.requireNonNull(channelWeights, "channelWeights"));
        for (Map.Entry<String, Integer> channel :
            channelWeights.entrySet()) {
            requireText(channel.getKey(), "channelName");
            int weight = Objects.requireNonNull(
                channel.getValue(),
                "channelWeight");
            if (weight < 0 || weight > 100) {
                throw new IllegalArgumentException(
                    "channel weight must be in 0..100");
            }
        }
        objectCapabilities = Objects.requireNonNull(
                objectCapabilities,
                "objectCapabilities")
            .stream()
            .sorted(java.util.Comparator
                .<ZLinkObjectCapability>comparingInt(
                    capability ->
                        capability.objectKind().value())
                .thenComparing(
                    ZLinkObjectCapability::stableType))
            .toList();
        if (objectCapabilities.size() > 1024) {
            throw new IllegalArgumentException(
                "objectCapabilities must contain at most 1024 entries");
        }
        if (objectRole != ZLinkMeshNodeObjectRole.SERVER
            && !objectCapabilities.isEmpty()) {
            throw new IllegalArgumentException(
                "Only a server descriptor may publish object capabilities");
        }
        Set<String> capabilityKeys = new HashSet<>();
        for (ZLinkObjectCapability capability : objectCapabilities) {
            Objects.requireNonNull(capability, "objectCapability");
            String capabilityKey =
                capability.objectKind().value()
                    + "\0"
                    + capability.stableType();
            if (!capabilityKeys.add(capabilityKey)) {
                throw new IllegalArgumentException(
                    "objectCapabilities must be unique by kind and stableType");
            }
        }
        Objects.requireNonNull(objectRole, "objectRole");
        Objects.requireNonNull(capacity, "capacity");
        maintenanceWave = Objects.requireNonNull(
            maintenanceWave,
            "maintenanceWave");
        maintenanceWave.ifPresent(
            value -> requireText(value, "maintenanceWave"));
        Objects.requireNonNull(state, "state");
        Objects.requireNonNull(updatedAt, "updatedAt");
    }

    private static String requireText(String value, String field) {
        if (value == null
            || value.isBlank()
            || value.indexOf('\0') >= 0) {
            throw new IllegalArgumentException(
                field + " must be non-blank text without NUL");
        }
        return value;
    }
}
