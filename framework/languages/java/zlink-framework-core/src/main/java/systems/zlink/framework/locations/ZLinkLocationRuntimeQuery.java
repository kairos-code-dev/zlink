package systems.zlink.framework.locations;

import java.util.List;
import java.util.concurrent.CompletionStage;

public interface ZLinkLocationRuntimeQuery {
    CompletionStage<ZLinkLocationRuntimeStatus> getStatusAsync();

    CompletionStage<List<ZLinkPeerLocation>> listPeersAsync(ZLinkPeerLocationFilter filter);

    CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRoutesAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationPage<ZLinkLocationTopologyEntry>> listTopologyAsync(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page);

    CompletionStage<List<ZLinkLocationServiceSummary>> listServiceSummariesAsync(
        ZLinkLocationServiceSummaryFilter filter);
}
