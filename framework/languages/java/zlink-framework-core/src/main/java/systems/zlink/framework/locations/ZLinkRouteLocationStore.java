package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkRouteLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteResult> removeRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkRouteLocation> resolveRouteAsync(ZLinkRouteLocationKey key);

    CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocationsAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page);
}
