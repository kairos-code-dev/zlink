package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkSpotLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateSpot(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteResult> removeSpot(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkSpotLocation> resolveSpot(ZLinkSpotLocationKey key);

    CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocations(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page);
}
