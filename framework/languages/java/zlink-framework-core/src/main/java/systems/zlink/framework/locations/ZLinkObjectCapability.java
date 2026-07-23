package systems.zlink.framework.locations;

import java.nio.charset.StandardCharsets;
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
        requireBoundedText(stableType, "stableType");
        Objects.requireNonNull(policy, "policy");
        if ((policy == ZLinkObjectMaintenancePolicyKind.SNAPSHOT)
            != hasSnapshotAdapter) {
            throw new IllegalArgumentException(
                "Snapshot policy and adapter presence must match");
        }
        placementProfiles = Set.copyOf(
            Objects.requireNonNull(
                placementProfiles,
                "placementProfiles"));
        if (placementProfiles.size() > 1024) {
            throw new IllegalArgumentException(
                "placementProfiles must contain at most 1024 entries");
        }
        for (String profile : placementProfiles) {
            requireBoundedText(profile, "placementProfile");
        }
        if (activeLimit != null && activeLimit <= 0) {
            throw new IllegalArgumentException(
                "activeLimit must be positive when present");
        }
        if (pendingLimit != null && pendingLimit <= 0) {
            throw new IllegalArgumentException(
                "pendingLimit must be positive when present");
        }
    }

    private static void requireBoundedText(String value, String field) {
        Objects.requireNonNull(value, field);
        int size = value.getBytes(StandardCharsets.UTF_8).length;
        if (value.isBlank()
            || value.indexOf('\0') >= 0
            || size > 255) {
            throw new IllegalArgumentException(
                field + " must be 1..255 UTF-8 bytes without NUL");
        }
    }
}
