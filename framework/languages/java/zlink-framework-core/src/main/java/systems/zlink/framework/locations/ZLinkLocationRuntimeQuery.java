package systems.zlink.framework.locations;

import java.util.List;
import java.util.concurrent.CompletionStage;

public interface ZLinkLocationRuntimeQuery {
    CompletionStage<ZLinkLocationRuntimeStatus> getStatus();

    CompletionStage<List<ZLinkPeerLocation>> listPeerLocations(ZLinkPeerLocationFilter filter);

    CompletionStage<ZLinkLocationPage<ZLinkSpotLocation>> listSpotLocations(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationPage<ZLinkActorLocation>> listActorLocations(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationPage<ZLinkRouteLocation>> listRouteLocations(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page);

    CompletionStage<ZLinkLocationPage<ZLinkLocationTopologyEntry>> listTopology(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page);

    CompletionStage<List<ZLinkLocationServiceSummary>> listServiceSummaries(
        ZLinkLocationServiceSummaryFilter filter);
}
