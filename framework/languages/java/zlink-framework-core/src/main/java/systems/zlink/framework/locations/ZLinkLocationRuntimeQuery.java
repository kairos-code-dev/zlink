package systems.zlink.framework.locations;

import java.util.List;
import java.util.concurrent.CompletionStage;

public interface ZLinkLocationRuntimeQuery {
    CompletionStage<ZLinkLocationRuntimeStatus> getStatusAsync();

    CompletionStage<List<ZLinkPeerLocation>> listPeerLocationsAsync(ZLinkPeerLocationFilter filter);

    CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocationsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocationsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocationsAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationPage<ZLinkLocationTopologyEntry>> listTopologyAsync(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page);

    CompletionStage<List<ZLinkLocationServiceSummary>> listServiceSummariesAsync(
        ZLinkLocationServiceSummaryFilter filter);
}
