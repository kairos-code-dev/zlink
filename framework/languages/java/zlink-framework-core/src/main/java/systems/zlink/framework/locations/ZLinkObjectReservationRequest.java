package systems.zlink.framework.locations;

import java.util.Objects;
import java.util.Optional;

public record ZLinkObjectReservationRequest(
    ZLinkPlacementObjectKind objectKind,
    String authorityKey,
    String stableType,
    Optional<String> placementProfile,
    Optional<String> affinityKey,
    String creationIntentReference,
    byte[] creationIntentHash,
    int creationIntentEncodedSize,
    ZLinkMeshNodeDescriptorKey targetDescriptor,
    ZLinkLocationOwnerToken targetOwner,
    int pendingCapacityDelta) {
    public ZLinkObjectReservationRequest {
        Objects.requireNonNull(objectKind, "objectKind");
        Objects.requireNonNull(authorityKey, "authorityKey");
        Objects.requireNonNull(stableType, "stableType");
        Objects.requireNonNull(placementProfile, "placementProfile");
        Objects.requireNonNull(affinityKey, "affinityKey");
        Objects.requireNonNull(
            creationIntentReference,
            "creationIntentReference");
        creationIntentHash = Objects.requireNonNull(
            creationIntentHash,
            "creationIntentHash").clone();
        Objects.requireNonNull(targetDescriptor, "targetDescriptor");
        Objects.requireNonNull(targetOwner, "targetOwner");
    }

    @Override
    public byte[] creationIntentHash() {
        return creationIntentHash.clone();
    }
}
