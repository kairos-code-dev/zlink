package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkPlacementAllocation(
    ZLinkPlacementAllocationState state,
    ZLinkPlacementObjectKind objectKind,
    String stableType,
    ZLinkMeshNodeDescriptorKey descriptor,
    long descriptorLifecycleGeneration,
    int capacityDelta) {
    public ZLinkPlacementAllocation {
        Objects.requireNonNull(state, "state");
        Objects.requireNonNull(objectKind, "objectKind");
        Objects.requireNonNull(stableType, "stableType");
        Objects.requireNonNull(descriptor, "descriptor");
        if (stableType.isBlank()) {
            throw new IllegalArgumentException(
                "stableType must not be blank");
        }
        if (descriptorLifecycleGeneration <= 0) {
            throw new IllegalArgumentException(
                "descriptorLifecycleGeneration must be positive");
        }
        if (capacityDelta <= 0) {
            throw new IllegalArgumentException(
                "capacityDelta must be in 1..Integer.MAX_VALUE");
        }
    }

}
