package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkSpotLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteResult> removeSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkSpotLocation> resolveSpotAsync(ZLinkSpotLocationKey key);

    CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocationsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page);
}
