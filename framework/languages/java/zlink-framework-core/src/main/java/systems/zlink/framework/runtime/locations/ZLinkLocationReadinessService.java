package systems.zlink.framework.runtime.locations;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.locations.ZLinkLocationKind;
import systems.zlink.framework.locations.ZLinkLocationPage;
import systems.zlink.framework.locations.ZLinkLocationReadiness;
import systems.zlink.framework.locations.ZLinkLocationRole;
import systems.zlink.framework.locations.ZLinkLocationRuntimeQuery;
import systems.zlink.framework.locations.ZLinkLocationTopologyEntry;
import systems.zlink.framework.locations.ZLinkLocationTopologyFilter;
import systems.zlink.framework.locations.ZLinkLocationTopologyState;
import systems.zlink.framework.locations.ZLinkPageRequest;

public final class ZLinkLocationReadinessService implements ZLinkLocationReadiness {
    private final ZLinkLocationRuntimeQuery query;

    public ZLinkLocationReadinessService(ZLinkLocationRuntimeQuery query) {
        this.query = query;
    }

    @Override
    public CompletionStage<Boolean> isPeerReadyAsync(
        String meshName,
        ZLinkLocationRole role,
        RoutingId nodeRid) {
        if (query == null) {
            return CompletableFuture.completedFuture(false);
        }
        return query.listTopologyAsync(
                new ZLinkLocationTopologyFilter(
                    ZLinkLocationKind.PEER,
                    meshName,
                    role,
                    nodeRid,
                    ZLinkLocationTopologyState.READY),
                ZLinkPageRequest.firstPage())
            .thenApply(ZLinkLocationPage::items)
            .thenApply(items -> items.stream().anyMatch(ZLinkLocationReadinessService::ready))
            .exceptionally(error -> false);
    }

    private static boolean ready(ZLinkLocationTopologyEntry entry) {
        return entry.state() == ZLinkLocationTopologyState.READY
            && entry.readyCount() > 0;
    }
}
