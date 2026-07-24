package systems.zlink.framework.locations;

import java.util.Objects;

public record ZLinkObjectReservationRequest(
    ZLinkPlacementObjectKind objectKind,
    String authorityKey,
    String stableType,
    String creationIntentReference,
    byte[] creationIntentHash,
    int creationIntentEncodedSize,
    ZLinkMeshNodeDescriptorKey targetDescriptor,
    long targetDescriptorLifecycleGeneration,
    ZLinkLocationOwnerToken targetOwner,
    byte[] creatingPayload,
    int pendingCapacityDelta) {
    public ZLinkObjectReservationRequest {
        Objects.requireNonNull(objectKind, "objectKind");
        Objects.requireNonNull(authorityKey, "authorityKey");
        Objects.requireNonNull(stableType, "stableType");
        if (authorityKey.isBlank()) {
            throw new IllegalArgumentException(
                "authorityKey must not be blank");
        }
        if (stableType.isBlank()) {
            throw new IllegalArgumentException(
                "stableType must not be blank");
        }
        Objects.requireNonNull(
            creationIntentReference,
            "creationIntentReference");
        creationIntentHash = Objects.requireNonNull(
            creationIntentHash,
            "creationIntentHash").clone();
        Objects.requireNonNull(targetDescriptor, "targetDescriptor");
        if (targetDescriptorLifecycleGeneration <= 0) {
            throw new IllegalArgumentException(
                "targetDescriptorLifecycleGeneration must be positive");
        }
        Objects.requireNonNull(targetOwner, "targetOwner");
        creatingPayload = Objects.requireNonNull(
            creatingPayload,
            "creatingPayload").clone();
        if (pendingCapacityDelta <= 0) {
            throw new IllegalArgumentException(
                "pendingCapacityDelta must be in 1..Integer.MAX_VALUE");
        }
    }

    @Override
    public byte[] creationIntentHash() {
        return creationIntentHash.clone();
    }

    @Override
    public byte[] creatingPayload() {
        return creatingPayload.clone();
    }
}
