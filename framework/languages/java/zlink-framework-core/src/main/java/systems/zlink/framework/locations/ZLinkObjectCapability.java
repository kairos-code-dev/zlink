package systems.zlink.framework.locations;

import java.util.Objects;
import java.util.Set;

public record ZLinkObjectCapability(
    ZLinkPlacementObjectKind objectKind,
    String stableType,
    ZLinkObjectMaintenancePolicyKind policy,
    boolean hasSnapshotAdapter,
    Set<String> placementProfiles,
    Integer activeLimit,
    Integer pendingLimit) {
    public ZLinkObjectCapability {
        Objects.requireNonNull(objectKind, "objectKind");
        Objects.requireNonNull(stableType, "stableType");
        Objects.requireNonNull(policy, "policy");
        placementProfiles = Set.copyOf(placementProfiles);
    }
}
