package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotManager;

public final class EnsureSpotRouteRequestHandler
    implements ZLinkRouteRequestHandler<Contracts.EnsureSpotRequest, Contracts.EnsureSpotReply> {
    private final ZLinkSpotManager spots;
    private final PlayEvidenceStore evidence;

    public EnsureSpotRouteRequestHandler(
        ZLinkSpotManager spots,
        PlayEvidenceStore evidence) {
        this.spots = spots;
        this.evidence = evidence;
    }

    @Override
    public Contracts.EnsureSpotReply handle(
        Contracts.EnsureSpotRequest request,
        ZLinkRouteRequestContext context) {
        spots.getOrCreate(ProbeSpot.class, RoutingId.from(request.spotRid()), "ensure")
            .toCompletableFuture()
            .join();
        String nodeRid = Env.get("ZLINK_KOTLIN_E2E_NODE_RID", "play-a");
        evidence.record(request.spotRid(), "spot-ensured", "node=" + nodeRid);
        return new Contracts.EnsureSpotReply(request.spotRid(), nodeRid);
    }
}
