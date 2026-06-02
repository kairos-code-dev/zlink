package systems.zlink.framework.spots;

public record ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind kind) {
    public ZLinkSpotActorChangeResult {
        if (kind == null) {
            throw new IllegalArgumentException("kind is required");
        }
    }
}
