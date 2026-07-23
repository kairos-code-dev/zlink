package systems.zlink.framework.locations;

public record ZLinkPlacementCapacity(
    int active,
    int pending,
    int activeLimit,
    int pendingLimit) {
}
