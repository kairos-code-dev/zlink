package systems.zlink.framework.locations;

public record ZLinkPlacementCapacity(
    int active,
    int pending,
    int activeLimit,
    int pendingLimit) {
    public ZLinkPlacementCapacity {
        if (active < 0
            || pending < 0
            || activeLimit <= 0
            || pendingLimit <= 0
            || active > activeLimit
            || pending > pendingLimit) {
            throw new IllegalArgumentException(
                "placement capacity counts and limits are invalid");
        }
    }
}
