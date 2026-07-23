package systems.zlink.framework.configuration;

import java.util.Set;
import java.util.Objects;

public record ZLinkObjectPlacementOptions(
    Set<String> placementProfiles,
    Integer maxActiveObjects,
    Integer maxPendingActivations) {
    public ZLinkObjectPlacementOptions {
        placementProfiles = Set.copyOf(
            Objects.requireNonNull(
                placementProfiles,
                "placementProfiles"));
    }
}
