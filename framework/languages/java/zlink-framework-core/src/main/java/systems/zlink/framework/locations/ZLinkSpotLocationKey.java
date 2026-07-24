package systems.zlink.framework.locations;

public record ZLinkSpotLocationKey(String spotId) {
    public ZLinkSpotLocationKey {
        systems.zlink.framework.runtime.internal.spots.ZLinkSpotIdValidator
            .requireValid(spotId);
    }

}
