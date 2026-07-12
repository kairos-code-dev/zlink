package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkRouteLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updateRoute(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteResult> removeRoute(
        ZLinkRouteLocationKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<ZLinkRouteLocation> resolveRoute(ZLinkRouteLocationKey key);

    CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocations(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page);
}
